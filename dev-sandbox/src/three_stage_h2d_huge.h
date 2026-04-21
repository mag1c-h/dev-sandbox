#ifndef THREE_STAGE_H2D_HUGE_H
#define THREE_STAGE_H2D_HUGE_H

#include <cstddef>
#include <cstdint>

#ifdef USE_NPU
#include <acl/acl.h>
#else
typedef void* aclrtStream;
#endif

class FftsDispatcherMinimal;

#ifdef __cplusplus
extern "C" {
#endif

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
    FftsDispatcherMinimal* dispatcher);

#ifdef __cplusplus
}
#endif

#endif