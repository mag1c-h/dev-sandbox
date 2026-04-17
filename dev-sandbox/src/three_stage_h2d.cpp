#include "ffts_dispatcher_minimal.h"
#include <acl/acl.h>
#include <vector>
#include <cstring>
#include <numeric>

extern int ThreeStageH2D(
    void** hostRawPtrs,
    void** deviceDestPtrs,
    size_t* sizes,
    size_t count,
    uint32_t deviceId,
    aclrtStream h2dStream,
    aclrtStream fftsStream,
    FftsDispatcherMinimal* dispatcher);

int ThreeStageH2D(
    void** hostRawPtrs,
    void** deviceDestPtrs,
    size_t* sizes,
    size_t count,
    uint32_t deviceId,
    aclrtStream h2dStream,
    aclrtStream fftsStream,
    FftsDispatcherMinimal* dispatcher)
{
    aclrtSetDevice(deviceId);
    
    if (count == 0) return 0;
    
    size_t totalSize = 0;
    std::vector<size_t> offsets(count);
    for (size_t i = 0; i < count; i++) {
        offsets[i] = totalSize;
        totalSize += sizes[i];
    }
    
    void* hostPinBuffer = nullptr;
    aclError aclRet = aclrtMallocHost(&hostPinBuffer, totalSize);
    if (aclRet != ACL_SUCCESS || hostPinBuffer == nullptr) {
        return aclRet;
    }
    
    void* devicePinBuffer = nullptr;
    aclRet = aclrtMalloc(&devicePinBuffer, totalSize, ACL_MEM_MALLOC_HUGE_FIRST);
    if (aclRet != ACL_SUCCESS) {
        aclrtFreeHost(hostPinBuffer);
        return aclRet;
    }
    
    for (size_t i = 0; i < count; i++) {
        void* dst = static_cast<uint8_t*>(hostPinBuffer) + offsets[i];
        std::memcpy(dst, hostRawPtrs[i], sizes[i]);
    }
    
    aclRet = aclrtMemcpyAsync(
        devicePinBuffer, totalSize,
        hostPinBuffer, totalSize,
        ACL_MEMCPY_HOST_TO_DEVICE, h2dStream);
    if (aclRet != ACL_SUCCESS) {
        aclrtFreeHost(hostPinBuffer);
        aclrtFree(devicePinBuffer);
        return aclRet;
    }
    
    aclRet = aclrtSynchronizeStream(h2dStream);
    if (aclRet != ACL_SUCCESS) {
        aclrtFreeHost(hostPinBuffer);
        aclrtFree(devicePinBuffer);
        return aclRet;
    }
    
    dispatcher->ReuseCtx(0);
    
    const size_t MAX_PARALLEL = 8;
    std::vector<uint32_t> taskIds(count);
    std::vector<int32_t> lastTaskId(MAX_PARALLEL, -1);
    
    for (size_t i = 0; i < count; i++) {
        void* src = static_cast<uint8_t*>(devicePinBuffer) + offsets[i];
        int ret = dispatcher->MemcpyAsync(
            deviceDestPtrs[i],
            src,
            sizes[i],
            &taskIds[i]);
        if (ret != 0) {
            aclrtFreeHost(hostPinBuffer);
            aclrtFree(devicePinBuffer);
            return ret;
        }
    }
    
    uint16_t readyCount = (count < MAX_PARALLEL) ? count : MAX_PARALLEL;
    int ret = dispatcher->LaunchFftsTask(fftsStream, readyCount, 0);
    if (ret != 0) {
        aclrtFreeHost(hostPinBuffer);
        aclrtFree(devicePinBuffer);
        return ret;
    }
    
    dispatcher->ReuseCtx(0);
    
    aclRet = aclrtSynchronizeStream(fftsStream);
    
    aclrtFreeHost(hostPinBuffer);
    aclrtFree(devicePinBuffer);
    
    return (aclRet == ACL_SUCCESS) ? 0 : aclRet;
}