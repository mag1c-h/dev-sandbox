#include "three_stage_h2d_huge.h"
#include "ffts_dispatcher_minimal.h"
#include <acl/acl.h>
#include <runtime/dev.h>
#include <vector>
#include <cstring>
#include <algorithm>

int ThreeStageH2D_Huge(
    void** hostPinPtrs,
    void** deviceDestPtrs,
    size_t* objectSizes,
    size_t* blobSizes,
    size_t* blobCounts,
    size_t* blobOffsets,
    size_t objectCount,
    uint32_t deviceId,
    aclrtStream h2dStream,
    aclrtStream fftsStream,
    FftsDispatcherMinimal* dispatcher)
{
    aclrtSetDevice(deviceId);
    
    if (objectCount == 0) return 0;
    
    size_t maxSize = 0;
    for (size_t i = 0; i < objectCount; i++) {
        maxSize = std::max(maxSize, objectSizes[i]);
    }
    
    if (maxSize == 0) return 0;
    
    void* devicePinBuffers[2] = {nullptr, nullptr};
    aclError aclRet = aclrtMalloc(&devicePinBuffers[0], maxSize, ACL_MEM_MALLOC_HUGE_FIRST);
    if (aclRet != ACL_SUCCESS) return aclRet;
    
    aclRet = aclrtMalloc(&devicePinBuffers[1], maxSize, ACL_MEM_MALLOC_HUGE_FIRST);
    if (aclRet != ACL_SUCCESS) {
        aclrtFree(devicePinBuffers[0]);
        return aclRet;
    }
    
    rtNotify_t toPinDone[2] = {nullptr, nullptr};
    rtNotify_t toDestDone[2] = {nullptr, nullptr};
    
    for (int i = 0; i < 2; i++) {
        int rtRet = rtNotifyCreate(static_cast<int32_t>(deviceId), &toPinDone[i]);
        if (rtRet != 0) {
            for (int j = 0; j < i; j++) {
                rtNotifyDestroy(toPinDone[j]);
                rtNotifyDestroy(toDestDone[j]);
            }
            aclrtFree(devicePinBuffers[0]);
            aclrtFree(devicePinBuffers[1]);
            return rtRet;
        }
        rtRet = rtNotifyCreate(static_cast<int32_t>(deviceId), &toDestDone[i]);
        if (rtRet != 0) {
            rtNotifyDestroy(toPinDone[i]);
            for (int j = 0; j < i; j++) {
                rtNotifyDestroy(toPinDone[j]);
                rtNotifyDestroy(toDestDone[j]);
            }
            aclrtFree(devicePinBuffers[0]);
            aclrtFree(devicePinBuffers[1]);
            return rtRet;
        }
    }
    
    int rtRet = rtNotifyRecord(toDestDone[0], fftsStream);
    if (rtRet != 0) {
        rtRet = rtNotifyRecord(toDestDone[0], h2dStream);
    }
    rtRet = rtNotifyRecord(toDestDone[1], fftsStream);
    if (rtRet != 0) {
        rtRet = rtNotifyRecord(toDestDone[1], h2dStream);
    }
    
    const size_t MAX_PARALLEL = 8;
    
    for (size_t objIdx = 0; objIdx < objectCount; objIdx++) {
        size_t slot = objIdx % 2;
        
        rtRet = rtNotifyWait(toDestDone[slot], h2dStream);
        if (rtRet != 0) {
            for (int i = 0; i < 2; i++) {
                rtNotifyDestroy(toPinDone[i]);
                rtNotifyDestroy(toDestDone[i]);
            }
            aclrtFree(devicePinBuffers[0]);
            aclrtFree(devicePinBuffers[1]);
            return rtRet;
        }
        
        aclRet = aclrtMemcpyAsync(
            devicePinBuffers[slot], maxSize,
            hostPinPtrs[objIdx], objectSizes[objIdx],
            ACL_MEMCPY_HOST_TO_DEVICE, h2dStream);
        if (aclRet != ACL_SUCCESS) {
            for (int i = 0; i < 2; i++) {
                rtNotifyDestroy(toPinDone[i]);
                rtNotifyDestroy(toDestDone[i]);
            }
            aclrtFree(devicePinBuffers[0]);
            aclrtFree(devicePinBuffers[1]);
            return aclRet;
        }
        
        rtRet = rtNotifyRecord(toPinDone[slot], h2dStream);
        if (rtRet != 0) {
            for (int i = 0; i < 2; i++) {
                rtNotifyDestroy(toPinDone[i]);
                rtNotifyDestroy(toDestDone[i]);
            }
            aclrtFree(devicePinBuffers[0]);
            aclrtFree(devicePinBuffers[1]);
            return rtRet;
        }
        
        rtRet = rtNotifyWait(toPinDone[slot], fftsStream);
        if (rtRet != 0) {
            for (int i = 0; i < 2; i++) {
                rtNotifyDestroy(toPinDone[i]);
                rtNotifyDestroy(toDestDone[i]);
            }
            aclrtFree(devicePinBuffers[0]);
            aclrtFree(devicePinBuffers[1]);
            return rtRet;
        }
        
        dispatcher->ReuseCtx(0);
        
        std::vector<int32_t> lastTaskId(MAX_PARALLEL, -1);
        std::vector<uint32_t> taskIds(blobCounts[objIdx]);
        
        size_t blobStartIdx = blobOffsets[objIdx];
        size_t offsetInPinBuffer = 0;
        
        for (size_t blobIdx = 0; blobIdx < blobCounts[objIdx]; blobIdx++) {
            size_t globalBlobIdx = blobStartIdx + blobIdx;
            void* src = static_cast<uint8_t*>(devicePinBuffers[slot]) + offsetInPinBuffer;
            
            int ret = dispatcher->MemcpyAsync(
                deviceDestPtrs[globalBlobIdx],
                src,
                blobSizes[globalBlobIdx],
                &taskIds[blobIdx]);
            if (ret != 0) {
                for (int i = 0; i < 2; i++) {
                    rtNotifyDestroy(toPinDone[i]);
                    rtNotifyDestroy(toDestDone[i]);
                }
                aclrtFree(devicePinBuffers[0]);
                aclrtFree(devicePinBuffers[1]);
                return ret;
            }
            
            size_t chainIdx = blobIdx % MAX_PARALLEL;
            if (lastTaskId[chainIdx] >= 0) {
                dispatcher->AddTaskDependency(lastTaskId[chainIdx], taskIds[blobIdx]);
            }
            lastTaskId[chainIdx] = taskIds[blobIdx];
            
            offsetInPinBuffer += blobSizes[globalBlobIdx];
        }
        
        uint16_t readyCount = std::min(static_cast<size_t>(blobCounts[objIdx]), MAX_PARALLEL);
        int ret = dispatcher->LaunchFftsTask(fftsStream, readyCount, 0);
        if (ret != 0) {
            for (int i = 0; i < 2; i++) {
                rtNotifyDestroy(toPinDone[i]);
                rtNotifyDestroy(toDestDone[i]);
            }
            aclrtFree(devicePinBuffers[0]);
            aclrtFree(devicePinBuffers[1]);
            return ret;
        }
        
        dispatcher->ReuseCtx(0);
        
        rtRet = rtNotifyRecord(toDestDone[slot], fftsStream);
        if (rtRet != 0) {
            for (int i = 0; i < 2; i++) {
                rtNotifyDestroy(toPinDone[i]);
                rtNotifyDestroy(toDestDone[i]);
            }
            aclrtFree(devicePinBuffers[0]);
            aclrtFree(devicePinBuffers[1]);
            return rtRet;
        }
    }
    
    aclRet = aclrtSynchronizeStream(h2dStream);
    if (aclRet != ACL_SUCCESS) {
        for (int i = 0; i < 2; i++) {
            rtNotifyDestroy(toPinDone[i]);
            rtNotifyDestroy(toDestDone[i]);
        }
        aclrtFree(devicePinBuffers[0]);
        aclrtFree(devicePinBuffers[1]);
        return aclRet;
    }
    
    aclRet = aclrtSynchronizeStream(fftsStream);
    if (aclRet != ACL_SUCCESS) {
        for (int i = 0; i < 2; i++) {
            rtNotifyDestroy(toPinDone[i]);
            rtNotifyDestroy(toDestDone[i]);
        }
        aclrtFree(devicePinBuffers[0]);
        aclrtFree(devicePinBuffers[1]);
        return aclRet;
    }
    
    for (int i = 0; i < 2; i++) {
        rtNotifyDestroy(toPinDone[i]);
        rtNotifyDestroy(toDestDone[i]);
    }
    aclrtFree(devicePinBuffers[0]);
    aclrtFree(devicePinBuffers[1]);
    
    return 0;
}