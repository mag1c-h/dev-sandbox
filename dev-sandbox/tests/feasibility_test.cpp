#include <acl/acl.h>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#include "ffts_dispatcher_minimal.h"

extern int DirectDiscreteH2D(void** hostPinPtrs, void** devicePtrs, size_t* sizes, size_t count, uint32_t deviceId);
extern int AsyncPipelineH2D(void** hostPinPtrs, void** devicePtrs, size_t* sizes, size_t count, uint32_t deviceId, aclrtStream stream);
extern int FftsH2D(void** hostPinPtrs, void** devicePtrs, size_t* sizes, size_t count, uint32_t deviceId, aclrtStream stream, FftsDispatcherMinimal* dispatcher);
extern int ThreeStageH2D(void** hostRawPtrs, void** deviceDestPtrs, size_t* sizes, size_t count, uint32_t deviceId, aclrtStream h2dStream, aclrtStream fftsStream, FftsDispatcherMinimal* dispatcher);

void PrintResult(const char* name, bool success) {
    if (success) {
        printf("  ✅ %s feasible\n", name);
    } else {
        printf("  ❌ %s failed\n", name);
    }
}

bool VerifyData(void* hostPtr, void* devicePtr, size_t size, char expectedChar) {
    std::vector<char> buffer(size);
    aclError ret = aclrtMemcpy(buffer.data(), size, devicePtr, size, ACL_MEMCPY_DEVICE_TO_HOST);
    if (ret != ACL_SUCCESS) {
        return false;
    }
    return buffer[0] == expectedChar;
}

int TestDirectDiscrete() {
    printf("\n=== Testing Direct Discrete H2D ===\n");
    
    const size_t blobCount = 8;
    const size_t blobSize = 64 * 1024;
    
    std::vector<void*> hostPins(blobCount);
    std::vector<void*> devPtrs(blobCount);
    std::vector<size_t> sizes(blobCount, blobSize);
    
    for (size_t i = 0; i < blobCount; i++) {
        aclError ret = aclrtMallocHost(&hostPins[i], blobSize);
        if (ret != ACL_SUCCESS) {
            printf("  ❌ aclrtMallocHost failed: %d\n", ret);
            return -1;
        }
        
        ret = aclrtMalloc(&devPtrs[i], blobSize, ACL_MEM_MALLOC_HUGE_FIRST);
        if (ret != ACL_SUCCESS) {
            printf("  ❌ aclrtMalloc failed: %d\n", ret);
            return -1;
        }
        
        std::memset(hostPins[i], 'A' + i, blobSize);
    }
    
    int ret = DirectDiscreteH2D(hostPins.data(), devPtrs.data(), sizes.data(), blobCount, 0);
    
    bool success = (ret == 0);
    if (success) {
        for (size_t i = 0; i < blobCount; i++) {
            if (!VerifyData(hostPins[i], devPtrs[i], blobSize, 'A' + i)) {
                success = false;
                break;
            }
        }
    }
    
    PrintResult("Direct discrete H2D", success);
    
    for (size_t i = 0; i < blobCount; i++) {
        aclrtFreeHost(hostPins[i]);
        aclrtFree(devPtrs[i]);
    }
    
    return success ? 0 : -1;
}

int TestAsyncPipeline() {
    printf("\n=== Testing Async Pipeline H2D ===\n");
    
    const size_t blobCount = 8;
    const size_t blobSize = 64 * 1024;
    
    std::vector<void*> hostPins(blobCount);
    std::vector<void*> devPtrs(blobCount);
    std::vector<size_t> sizes(blobCount, blobSize);
    
    aclrtStream stream;
    aclError aclRet = aclrtCreateStream(&stream);
    if (aclRet != ACL_SUCCESS) {
        printf("  ❌ aclrtCreateStream failed: %d\n", aclRet);
        return -1;
    }
    
    for (size_t i = 0; i < blobCount; i++) {
        aclRet = aclrtMallocHost(&hostPins[i], blobSize);
        if (aclRet != ACL_SUCCESS) return -1;
        
        aclRet = aclrtMalloc(&devPtrs[i], blobSize, ACL_MEM_MALLOC_HUGE_FIRST);
        if (aclRet != ACL_SUCCESS) return -1;
        
        std::memset(hostPins[i], 'B' + i, blobSize);
    }
    
    int ret = AsyncPipelineH2D(hostPins.data(), devPtrs.data(), sizes.data(), blobCount, 0, stream);
    
    bool success = (ret == 0);
    if (success) {
        for (size_t i = 0; i < blobCount; i++) {
            if (!VerifyData(hostPins[i], devPtrs[i], blobSize, 'B' + i)) {
                success = false;
                break;
            }
        }
    }
    
    PrintResult("Async pipeline H2D", success);
    
    aclrtDestroyStream(stream);
    for (size_t i = 0; i < blobCount; i++) {
        aclrtFreeHost(hostPins[i]);
        aclrtFree(devPtrs[i]);
    }
    
    return success ? 0 : -1;
}

int TestFftsH2D() {
    printf("\n=== Testing FFTS H2D (Hardware Dependency) ===\n");
    
    const size_t blobCount = 8;
    const size_t blobSize = 64 * 1024;
    
    std::vector<void*> hostPins(blobCount);
    std::vector<void*> devPtrs(blobCount);
    std::vector<size_t> sizes(blobCount, blobSize);
    
    aclrtStream stream;
    aclError aclRet = aclrtCreateStream(&stream);
    if (aclRet != ACL_SUCCESS) return -1;
    
    for (size_t i = 0; i < blobCount; i++) {
        aclRet = aclrtMallocHost(&hostPins[i], blobSize);
        if (aclRet != ACL_SUCCESS) return -1;
        
        aclRet = aclrtMalloc(&devPtrs[i], blobSize, ACL_MEM_MALLOC_HUGE_FIRST);
        if (aclRet != ACL_SUCCESS) return -1;
        
        std::memset(hostPins[i], 'C' + i, blobSize);
    }
    
    FftsDispatcherMinimal dispatcher;
    dispatcher.Init();
    dispatcher.CreateFftsCtxs(1);
    dispatcher.SetFftsCtx(0);
    
    int ret = FftsH2D(hostPins.data(), devPtrs.data(), sizes.data(), blobCount, 0, stream, &dispatcher);
    
    bool success = (ret == 0);
    if (success) {
        for (size_t i = 0; i < blobCount; i++) {
            if (!VerifyData(hostPins[i], devPtrs[i], blobSize, 'C' + i)) {
                success = false;
                break;
            }
        }
    }
    
    if (success) {
        PrintResult("FFTS H2D (Host→Device)", true);
        printf("  🎉 FFTS supports Host→Device direct copy!\n");
    } else {
        PrintResult("FFTS H2D (Host→Device)", false);
        printf("  ⚠️  FFTS may not support Host→Device (hardware limitation)\n");
    }
    
    aclrtDestroyStream(stream);
    for (size_t i = 0; i < blobCount; i++) {
        aclrtFreeHost(hostPins[i]);
        aclrtFree(devPtrs[i]);
    }
    
    return 0;
}

int TestThreeStage() {
    printf("\n=== Testing Three Stage H2D (H2H+H2D+D2D) ===\n");
    
    const size_t blobCount = 8;
    const size_t blobSize = 64 * 1024;
    
    std::vector<void*> hostRawPtrs(blobCount);
    std::vector<void*> devPtrs(blobCount);
    std::vector<size_t> sizes(blobCount, blobSize);
    
    for (size_t i = 0; i < blobCount; i++) {
        hostRawPtrs[i] = std::malloc(blobSize);
        if (hostRawPtrs[i] == nullptr) return -1;
        
        aclError aclRet = aclrtMalloc(&devPtrs[i], blobSize, ACL_MEM_MALLOC_HUGE_FIRST);
        if (aclRet != ACL_SUCCESS) return -1;
        
        std::memset(hostRawPtrs[i], 'D' + i, blobSize);
    }
    
    aclrtStream h2dStream, fftsStream;
    aclrtCreateStream(&h2dStream);
    aclrtCreateStream(&fftsStream);
    
    FftsDispatcherMinimal dispatcher;
    dispatcher.Init();
    dispatcher.CreateFftsCtxs(1);
    dispatcher.SetFftsCtx(0);
    
    int ret = ThreeStageH2D(hostRawPtrs.data(), devPtrs.data(), sizes.data(), blobCount, 0, h2dStream, fftsStream, &dispatcher);
    
    bool success = (ret == 0);
    if (success) {
        for (size_t i = 0; i < blobCount; i++) {
            std::vector<char> verify(blobSize);
            aclrtMemcpy(verify.data(), blobSize, devPtrs[i], blobSize, ACL_MEMCPY_DEVICE_TO_HOST);
            if (verify[0] != 'D' + i) {
                success = false;
                break;
            }
        }
    }
    
    PrintResult("Three stage H2D", success);
    
    aclrtDestroyStream(h2dStream);
    aclrtDestroyStream(fftsStream);
    for (size_t i = 0; i < blobCount; i++) {
        std::free(hostRawPtrs[i]);
        aclrtFree(devPtrs[i]);
    }
    
    return success ? 0 : -1;
}

int main() {
    printf("========================================\n");
    printf("  Feasibility Test: Discrete→Discrete H2D\n");
    printf("========================================\n");
    
    aclError ret = aclInit(nullptr);
    if (ret != ACL_SUCCESS) {
        printf("❌ aclInit failed: %d\n", ret);
        printf("Please ensure CANN is installed and environment is set:\n");
        printf("  source /usr/local/Ascend/ascend-toolkit/set_env.sh\n");
        return 1;
    }
    
    ret = aclrtSetDevice(0);
    if (ret != ACL_SUCCESS) {
        printf("❌ aclrtSetDevice failed: %d\n", ret);
        printf("Please ensure NPU device is available\n");
        aclFinalize();
        return 1;
    }
    
    printf("✅ ACL initialized, device 0 set\n");
    
    int result = 0;
    result |= TestDirectDiscrete();
    result |= TestAsyncPipeline();
    result |= TestFftsH2D();
    result |= TestThreeStage();
    
    printf("\n========================================\n");
    printf("  Summary:\n");
    if (result == 0) {
        printf("  ✅ All tests passed\n");
    } else {
        printf("  ⚠️  Some tests failed (check above)\n");
    }
    printf("========================================\n");
    
    aclrtResetDevice(0);
    aclFinalize();
    
    return 0;
}