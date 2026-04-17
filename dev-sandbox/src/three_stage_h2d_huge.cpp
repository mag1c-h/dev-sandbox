#include "three_stage_h2d_huge.h"
#include "ffts_dispatcher_minimal.h"
#include <acl/acl.h>
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
    
    aclrtNotify toPinDone[2] = {nullptr, nullptr};
    aclrtNotify toDestDone[2] = {nullptr, nullptr};
    
    for (int i = 0; i < 2; i++) {
        aclRet = aclrtNotifyCreate(&toPinDone[i]);
        if (aclRet != ACL_SUCCESS) {
            for (int j = 0; j < i; j++) {
                aclrtNotifyDestroy(toPinDone[j]);
                aclrtNotifyDestroy(toDestDone[j]);
            }
            aclrtFree(devicePinBuffers[0]);
            aclrtFree(devicePinBuffers[1]);
            return aclRet;
        }
        aclRet = aclrtNotifyCreate(&toDestDone[i]);
        if (aclRet != ACL_SUCCESS) {
            aclrtNotifyDestroy(toPinDone[i]);
            for (int j = 0; j < i; j++) {
                aclrtNotifyDestroy(toPinDone[j]);
                aclrtNotifyDestroy(toDestDone[j]);
            }
            aclrtFree(devicePinBuffers[0]);
            aclrtFree(devicePinBuffers[1]);
            return aclRet;
        }
    }
    
    aclRet = aclrtNotifyRecord(toDestDone[0], fftsStream);
    if (aclRet != ACL_SUCCESS) {
        aclRet = aclrtNotifyRecord(toDestDone[0], h2dStream);
    }
    aclRet = aclrtNotifyRecord(toDestDone[1], fftsStream);
    if (aclRet != ACL_SUCCESS) {
        aclRet = aclrtNotifyRecord(toDestDone[1], h2dStream);
    }
    
    const size_t MAX_PARALLEL = 8;
    
    for (size_t objIdx = 0; objIdx < objectCount; objIdx++) {
        size_t slot = objIdx % 2;
        
        aclRet = aclrtNotifyWait(toDestDone[slot], h2dStream);
        if (aclRet != ACL_SUCCESS) {
            for (int i = 0; i < 2; i++) {
                aclrtNotifyDestroy(toPinDone[i]);
                aclrtNotifyDestroy(toDestDone[i]);
            }
            aclrtFree(devicePinBuffers[0]);
            aclrtFree(devicePinBuffers[1]);
            return aclRet;
        }
        
        aclRet = aclrtMemcpyAsync(
            devicePinBuffers[slot], maxSize,
            hostPinPtrs[objIdx], objectSizes[objIdx],
            ACL_MEMCPY_HOST_TO_DEVICE, h2dStream);
        if (aclRet != ACL_SUCCESS) {
            for (int i = 0; i < 2; i++) {
                aclrtNotifyDestroy(toPinDone[i]);
                aclrtNotifyDestroy(toDestDone[i]);
            }
            aclrtFree(devicePinBuffers[0]);
            aclrtFree(devicePinBuffers[1]);
            return aclRet;
        }
        
        aclRet = aclrtNotifyRecord(toPinDone[slot], h2dStream);
        if (aclRet != ACL_SUCCESS) {
            for (int i = 0; i < 2; i++) {
                aclrtNotifyDestroy(toPinDone[i]);
                aclrtNotifyDestroy(toDestDone[i]);
            }
            aclrtFree(devicePinBuffers[0]);
            aclrtFree(devicePinBuffers[1]);
            return aclRet;
        }
        
        aclRet = aclrtNotifyWait(toPinDone[slot], fftsStream);
        if (aclRet != ACL_SUCCESS) {
            for (int i = 0; i < 2; i++) {
                aclrtNotifyDestroy(toPinDone[i]);
                aclrtNotifyDestroy(toDestDone[i]);
            }
            aclrtFree(devicePinBuffers[0]);
            aclrtFree(devicePinBuffers[1]);
            return aclRet;
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
                    aclrtNotifyDestroy(toPinDone[i]);
                    aclrtNotifyDestroy(toDestDone[i]);
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
                aclrtNotifyDestroy(toPinDone[i]);
                aclrtNotifyDestroy(toDestDone[i]);
            }
            aclrtFree(devicePinBuffers[0]);
            aclrtFree(devicePinBuffers[1]);
            return ret;
        }
        
        dispatcher->ReuseCtx(0);
        
        aclRet = aclrtNotifyRecord(toDestDone[slot], fftsStream);
        if (aclRet != ACL_SUCCESS) {
            for (int i = 0; i < 2; i++) {
                aclrtNotifyDestroy(toPinDone[i]);
                aclrtNotifyDestroy(toDestDone[i]);
            }
            aclrtFree(devicePinBuffers[0]);
            aclrtFree(devicePinBuffers[1]);
            return aclRet;
        }
    }
    
    aclRet = aclrtSynchronizeStream(h2dStream);
    if (aclRet != ACL_SUCCESS) {
        for (int i = 0; i < 2; i++) {
            aclrtNotifyDestroy(toPinDone[i]);
            aclrtNotifyDestroy(toDestDone[i]);
        }
        aclrtFree(devicePinBuffers[0]);
        aclrtFree(devicePinBuffers[1]);
        return aclRet;
    }
    
    aclRet = aclrtSynchronizeStream(fftsStream);
    if (aclRet != ACL_SUCCESS) {
        for (int i = 0; i < 2; i++) {
            aclrtNotifyDestroy(toPinDone[i]);
            aclrtNotifyDestroy(toDestDone[i]);
        }
        aclrtFree(devicePinBuffers[0]);
        aclrtFree(devicePinBuffers[1]);
        return aclRet;
    }
    
    for (int i = 0; i < 2; i++) {
        aclrtNotifyDestroy(toPinDone[i]);
        aclrtNotifyDestroy(toDestDone[i]);
    }
    aclrtFree(devicePinBuffers[0]);
    aclrtFree(devicePinBuffers[1]);
    
    return 0;
}