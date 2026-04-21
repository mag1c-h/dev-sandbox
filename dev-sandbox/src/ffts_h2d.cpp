#include "ffts_dispatcher_minimal.h"

#ifdef USE_NPU
#include <acl/acl.h>
#endif

#include <cstdint>
#include <vector>

#ifndef USE_NPU
typedef int aclError;
constexpr aclError ACL_SUCCESS = 0;
inline aclError aclrtSetDevice(uint32_t) { return ACL_SUCCESS; }
inline aclError aclrtSynchronizeStream(void*) { return ACL_SUCCESS; }
#endif

extern int FftsH2D(
    void** hostPinPtrs,
    void** devicePtrs,
    size_t* sizes,
    size_t count,
    uint32_t deviceId,
    aclrtStream stream,
    FftsDispatcherMinimal* dispatcher);

int FftsH2D(
    void** hostPinPtrs,
    void** devicePtrs,
    size_t* sizes,
    size_t count,
    uint32_t deviceId,
    aclrtStream stream,
    FftsDispatcherMinimal* dispatcher)
{
    aclrtSetDevice(deviceId);
    
    const size_t MAX_PARALLEL = 8;
    std::vector<int32_t> lastTaskId(MAX_PARALLEL, -1);
    std::vector<uint32_t> taskIds(count);
    
    for (size_t i = 0; i < count; i++) {
        int ret = dispatcher->MemcpyAsync(
            devicePtrs[i],
            hostPinPtrs[i],
            sizes[i],
            &taskIds[i]);
        
        if (ret != 0) {
            return ret;
        }
        
        size_t idx = i % MAX_PARALLEL;
        lastTaskId[idx] = taskIds[i];
    }
    
    uint16_t readyCount = (count < MAX_PARALLEL) ? static_cast<uint16_t>(count) : static_cast<uint16_t>(MAX_PARALLEL);
    int ret = dispatcher->LaunchFftsTask(stream, readyCount, 0);
    if (ret != 0) {
        return ret;
    }
    
    dispatcher->ReuseCtx(0);
    
    aclError aclRet = aclrtSynchronizeStream(stream);
    return (aclRet == ACL_SUCCESS) ? 0 : aclRet;
}