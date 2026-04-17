#include <acl/acl.h>
#include <vector>
#include <cstring>

extern int DirectDiscreteH2D(
    void** hostPinPtrs,
    void** devicePtrs,
    size_t* sizes,
    size_t count,
    uint32_t deviceId);

int DirectDiscreteH2D(
    void** hostPinPtrs,
    void** devicePtrs,
    size_t* sizes,
    size_t count,
    uint32_t deviceId)
{
    aclrtSetDevice(deviceId);
    
    std::vector<aclrtMemcpyBatchAttr> attrs(count);
    std::vector<size_t> attrIndexes(count);
    
    aclrtMemLocation hostLoc = { 0, ACL_MEM_LOCATION_TYPE_HOST };
    aclrtMemLocation devLoc = { deviceId, ACL_MEM_LOCATION_TYPE_DEVICE };
    
    for (size_t i = 0; i < count; i++) {
        attrs[i].dstLoc = devLoc;
        attrs[i].srcLoc = hostLoc;
        std::memset(attrs[i].rsv, 0, sizeof(attrs[i].rsv));
        attrIndexes[i] = i;
    }
    
    size_t failIndex = 0;
    aclError ret = aclrtMemcpyBatch(
        devicePtrs, sizes,
        hostPinPtrs, sizes,
        count,
        attrs.data(), attrIndexes.data(), count,
        &failIndex);
    
    return (ret == ACL_SUCCESS) ? 0 : ret;
}