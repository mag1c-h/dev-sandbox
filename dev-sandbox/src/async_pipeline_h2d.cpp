#include <acl/acl.h>
#include <vector>

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