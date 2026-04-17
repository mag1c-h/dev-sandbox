#include <acl/acl.h>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <chrono>

#include "ffts_dispatcher_minimal.h"

extern int DirectDiscreteH2D(void** hostPinPtrs, void** devicePtrs, size_t* sizes, size_t count, uint32_t deviceId);
extern int AsyncPipelineH2D(void** hostPinPtrs, void** devicePtrs, size_t* sizes, size_t count, uint32_t deviceId, aclrtStream stream);
extern int FftsH2D(void** hostPinPtrs, void** devicePtrs, size_t* sizes, size_t count, uint32_t deviceId, aclrtStream stream, FftsDispatcherMinimal* dispatcher);
extern int ThreeStageH2D(void** hostRawPtrs, void** deviceDestPtrs, size_t* sizes, size_t count, uint32_t deviceId, aclrtStream h2dStream, aclrtStream fftsStream, FftsDispatcherMinimal* dispatcher);

double CalculateBandwidth(size_t totalBytes, double seconds) {
    return (double)totalBytes / seconds / (1024.0 * 1024.0 * 1024.0);
}

struct TestConfig {
    size_t blobCount;
    size_t blobSize;
};

void PrepareBuffers(std::vector<void*>& hostPins, std::vector<void*>& devPtrs, std::vector<size_t>& sizes,
                    size_t blobCount, size_t blobSize, char fillChar) {
    hostPins.resize(blobCount);
    devPtrs.resize(blobCount);
    sizes.resize(blobCount, blobSize);
    
    for (size_t i = 0; i < blobCount; i++) {
        aclrtMallocHost(&hostPins[i], blobSize);
        aclrtMalloc(&devPtrs[i], blobSize, ACL_MEM_MALLOC_HUGE_FIRST);
        std::memset(hostPins[i], fillChar + i, blobSize);
    }
}

void FreeBuffers(std::vector<void*>& hostPins, std::vector<void*>& devPtrs) {
    for (size_t i = 0; i < hostPins.size(); i++) {
        aclrtFreeHost(hostPins[i]);
        aclrtFree(devPtrs[i]);
    }
}

void BenchmarkDirectDiscrete(const TestConfig& config) {
    std::vector<void*> hostPins, devPtrs;
    std::vector<size_t> sizes;
    PrepareBuffers(hostPins, devPtrs, sizes, config.blobCount, config.blobSize, 'A');
    
    auto start = std::chrono::high_resolution_clock::now();
    int ret = DirectDiscreteH2D(hostPins.data(), devPtrs.data(), sizes.data(), config.blobCount, 0);
    auto end = std::chrono::high_resolution_clock::now();
    
    double seconds = std::chrono::duration<double>(end - start).count();
    size_t totalBytes = config.blobCount * config.blobSize;
    double bandwidth = CalculateBandwidth(totalBytes, seconds);
    
    printf("  Direct:  %.2f GB/s", bandwidth);
    if (ret != 0) printf(" (failed: %d)", ret);
    printf("\n");
    
    FreeBuffers(hostPins, devPtrs);
}

void BenchmarkAsyncPipeline(const TestConfig& config) {
    std::vector<void*> hostPins, devPtrs;
    std::vector<size_t> sizes;
    PrepareBuffers(hostPins, devPtrs, sizes, config.blobCount, config.blobSize, 'B');
    
    aclrtStream stream;
    aclrtCreateStream(&stream);
    
    auto start = std::chrono::high_resolution_clock::now();
    int ret = AsyncPipelineH2D(hostPins.data(), devPtrs.data(), sizes.data(), config.blobCount, 0, stream);
    auto end = std::chrono::high_resolution_clock::now();
    
    double seconds = std::chrono::duration<double>(end - start).count();
    size_t totalBytes = config.blobCount * config.blobSize;
    double bandwidth = CalculateBandwidth(totalBytes, seconds);
    
    printf("  Async:   %.2f GB/s", bandwidth);
    if (ret != 0) printf(" (failed: %d)", ret);
    printf("\n");
    
    aclrtDestroyStream(stream);
    FreeBuffers(hostPins, devPtrs);
}

void BenchmarkFftsH2D(const TestConfig& config, bool fftsSupported) {
    if (!fftsSupported) {
        printf("  FFTS:    N/A (not supported)\n");
        return;
    }
    
    std::vector<void*> hostPins, devPtrs;
    std::vector<size_t> sizes;
    PrepareBuffers(hostPins, devPtrs, sizes, config.blobCount, config.blobSize, 'C');
    
    aclrtStream stream;
    aclrtCreateStream(&stream);
    
    FftsDispatcherMinimal dispatcher;
    dispatcher.Init();
    dispatcher.CreateFftsCtxs(1);
    dispatcher.SetFftsCtx(0);
    
    auto start = std::chrono::high_resolution_clock::now();
    int ret = FftsH2D(hostPins.data(), devPtrs.data(), sizes.data(), config.blobCount, 0, stream, &dispatcher);
    auto end = std::chrono::high_resolution_clock::now();
    
    double seconds = std::chrono::duration<double>(end - start).count();
    size_t totalBytes = config.blobCount * config.blobSize;
    double bandwidth = CalculateBandwidth(totalBytes, seconds);
    
    printf("  FFTS:    %.2f GB/s", bandwidth);
    if (ret != 0) printf(" (failed: %d)", ret);
    printf("\n");
    
    aclrtDestroyStream(stream);
    FreeBuffers(hostPins, devPtrs);
}

void BenchmarkThreeStage(const TestConfig& config) {
    std::vector<void*> hostRawPtrs(config.blobCount);
    std::vector<void*> devPtrs(config.blobCount);
    std::vector<size_t> sizes(config.blobCount, config.blobSize);
    
    for (size_t i = 0; i < config.blobCount; i++) {
        hostRawPtrs[i] = std::malloc(config.blobSize);
        std::memset(hostRawPtrs[i], 'E' + i, config.blobSize);
        aclrtMalloc(&devPtrs[i], config.blobSize, ACL_MEM_MALLOC_HUGE_FIRST);
    }
    
    aclrtStream h2dStream, fftsStream;
    aclrtCreateStream(&h2dStream);
    aclrtCreateStream(&fftsStream);
    
    FftsDispatcherMinimal dispatcher;
    dispatcher.Init();
    dispatcher.CreateFftsCtxs(1);
    dispatcher.SetFftsCtx(0);
    
    auto start = std::chrono::high_resolution_clock::now();
    int ret = ThreeStageH2D(hostRawPtrs.data(), devPtrs.data(), sizes.data(), config.blobCount, 0, h2dStream, fftsStream, &dispatcher);
    auto end = std::chrono::high_resolution_clock::now();
    
    double seconds = std::chrono::duration<double>(end - start).count();
    size_t totalBytes = config.blobCount * config.blobSize;
    double bandwidth = CalculateBandwidth(totalBytes, seconds);
    
    printf("  3-Stage: %.2f GB/s", bandwidth);
    if (ret != 0) printf(" (failed: %d)", ret);
    printf("\n");
    
    aclrtDestroyStream(h2dStream);
    aclrtDestroyStream(fftsStream);
    for (size_t i = 0; i < config.blobCount; i++) {
        std::free(hostRawPtrs[i]);
        aclrtFree(devPtrs[i]);
    }
}

bool CheckFftsH2DSupport() {
    const size_t testCount = 2;
    const size_t testSize = 4 * 1024;
    
    std::vector<void*> hostPins(testCount);
    std::vector<void*> devPtrs(testCount);
    std::vector<size_t> sizes(testCount, testSize);
    
    for (size_t i = 0; i < testCount; i++) {
        aclrtMallocHost(&hostPins[i], testSize);
        aclrtMalloc(&devPtrs[i], testSize, ACL_MEM_MALLOC_HUGE_FIRST);
        std::memset(hostPins[i], 'X', testSize);
    }
    
    aclrtStream stream;
    aclrtCreateStream(&stream);
    
    FftsDispatcherMinimal dispatcher;
    dispatcher.Init();
    dispatcher.CreateFftsCtxs(1);
    dispatcher.SetFftsCtx(0);
    
    int ret = FftsH2D(hostPins.data(), devPtrs.data(), sizes.data(), testCount, 0, stream, &dispatcher);
    
    aclrtDestroyStream(stream);
    for (size_t i = 0; i < testCount; i++) {
        aclrtFreeHost(hostPins[i]);
        aclrtFree(devPtrs[i]);
    }
    
    return (ret == 0);
}

int main() {
    printf("========================================\n");
    printf("  Performance Benchmark\n");
    printf("========================================\n");
    
    aclError ret = aclInit(nullptr);
    if (ret != ACL_SUCCESS) {
        printf("❌ aclInit failed: %d\n", ret);
        return 1;
    }
    
    ret = aclrtSetDevice(0);
    if (ret != ACL_SUCCESS) {
        printf("❌ aclrtSetDevice failed: %d\n", ret);
        aclFinalize();
        return 1;
    }
    
    bool fftsSupported = CheckFftsH2DSupport();
    printf("FFTS H2D support: %s\n", fftsSupported ? "✅ Yes" : "❌ No");
    
    std::vector<TestConfig> configs = {
        { 1,   4 * 1024 },        // 1 blob, 4KB
        { 8,   64 * 1024 },       // 8 blobs, 64KB
        { 64, 1024 * 1024 },      // 64 blobs, 1MB
    };
    
    for (const auto& config : configs) {
        printf("\n--- BlobCount=%zu, BlobSize=%zuKB, Total=%.2fMB ---\n",
               config.blobCount, config.blobSize / 1024,
               (double)(config.blobCount * config.blobSize) / (1024.0 * 1024.0));
        
        BenchmarkDirectDiscrete(config);
        BenchmarkAsyncPipeline(config);
        BenchmarkFftsH2D(config, fftsSupported);
        BenchmarkThreeStage(config);
    }
    
    printf("\n========================================\n");
    printf("  Benchmark Complete\n");
    printf("========================================\n");
    
    aclrtResetDevice(0);
    aclFinalize();
    
    return 0;
}