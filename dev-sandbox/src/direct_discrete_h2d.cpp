#ifdef USE_NPU
#include <acl/acl.h>
#endif

#include <cstdint>
#include <vector>
#include <cstring>

#ifndef USE_NPU
typedef int aclError;
constexpr aclError ACL_SUCCESS = 0;
constexpr int ACL_MEMCPY_HOST_TO_DEVICE = 1;
constexpr int ACL_MEM_LOCATION_TYPE_HOST = 0;
constexpr int ACL_MEM_LOCATION_TYPE_DEVICE = 1;
typedef void* aclrtStream;
typedef struct {
    int deviceId;
    int type;
} aclrtMemLocation;
typedef struct {
    aclrtMemLocation dstLoc;
    aclrtMemLocation srcLoc;
    char rsv[32];
} aclrtMemcpyBatchAttr;
inline aclError aclrtSetDevice(uint32_t) { return ACL_SUCCESS; }
inline aclError aclrtMemcpyBatch(void**, uint64_t*, void**, uint64_t*, size_t,
                                  aclrtMemcpyBatchAttr*, uint64_t*, size_t, size_t*) { return ACL_SUCCESS; }
#endif

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
    std::vector<uint64_t> attrIndexes(count);
    
    aclrtMemLocation hostLoc = { 0, ACL_MEM_LOCATION_TYPE_HOST };
    aclrtMemLocation devLoc = { static_cast<int>(deviceId), ACL_MEM_LOCATION_TYPE_DEVICE };
    
    for (size_t i = 0; i < count; i++) {
        attrs[i].dstLoc = devLoc;
        attrs[i].srcLoc = hostLoc;
        std::memset(attrs[i].rsv, 0, sizeof(attrs[i].rsv));
        attrIndexes[i] = i;
    }
    
    size_t failIndex = 0;
    std::vector<uint64_t> sizeVec(count);
    for (size_t i = 0; i < count; i++) sizeVec[i] = sizes[i];
    
    aclError ret = aclrtMemcpyBatch(
        devicePtrs, sizeVec.data(),
        hostPinPtrs, sizeVec.data(),
        count,
        attrs.data(), attrIndexes.data(), count,
        &failIndex);
    
    return (ret == ACL_SUCCESS) ? 0 : ret;
}