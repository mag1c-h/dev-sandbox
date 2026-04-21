#ifdef USE_NPU
#include <acl/acl.h>
#endif

#include <cstdint>
#include <vector>

#ifndef USE_NPU
typedef int aclError;
constexpr aclError ACL_SUCCESS = 0;
constexpr int ACL_MEMCPY_HOST_TO_DEVICE = 1;
typedef void* aclrtStream;
inline aclError aclrtSetDevice(uint32_t) { return ACL_SUCCESS; }
inline aclError aclrtMemcpyAsync(void*, uint64_t, void*, uint64_t, int, aclrtStream) { return ACL_SUCCESS; }
inline aclError aclrtSynchronizeStream(aclrtStream) { return ACL_SUCCESS; }
#endif

extern int AsyncPipelineH2D(
    void** hostPinPtrs,
    void** devicePtrs,
    size_t* sizes,
    size_t count,
    uint32_t deviceId,
    aclrtStream stream);

int AsyncPipelineH2D(
    void** hostPinPtrs,
    void** devicePtrs,
    size_t* sizes,
    size_t count,
    uint32_t deviceId,
    aclrtStream stream)
{
    aclrtSetDevice(deviceId);
    
    for (size_t i = 0; i < count; i++) {
        aclError ret = aclrtMemcpyAsync(
            devicePtrs[i], sizes[i],
            hostPinPtrs[i], sizes[i],
            ACL_MEMCPY_HOST_TO_DEVICE,
            stream);
        
        if (ret != ACL_SUCCESS) {
            return ret;
        }
    }
    
    aclError ret = aclrtSynchronizeStream(stream);
    return (ret == ACL_SUCCESS) ? 0 : ret;
}