#ifdef USE_NPU
#include <acl/acl.h>
#else
#include <cstddef>
#include <cstdint>
typedef int aclError;
constexpr aclError ACL_SUCCESS = 0;
constexpr int ACL_MEMCPY_HOST_TO_DEVICE = 1;
constexpr int ACL_MEM_MALLOC_HUGE_FIRST = 0;
typedef void* aclrtStream;
inline aclError aclrtSetDevice(uint32_t) { return ACL_SUCCESS; }
inline aclError aclrtCreateStream(aclrtStream*) { return ACL_SUCCESS; }
inline void aclrtDestroyStream(aclrtStream) {}
inline aclError aclrtMalloc(void**, size_t, int) { return ACL_SUCCESS; }
inline void aclrtFree(void*) {}
inline aclError aclrtMallocHost(void**, size_t) { return ACL_SUCCESS; }
inline void aclrtFreeHost(void*) {}
inline aclError aclrtSynchronizeStream(aclrtStream) { return ACL_SUCCESS; }
inline aclError aclInit(void*) { return ACL_SUCCESS; }
inline void aclFinalize() {}
inline aclError aclrtResetDevice(uint32_t) { return ACL_SUCCESS; }
#endif

#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <memory>

#include "ffts_dispatcher_minimal.h"
#include "simple_thread_pool.h"
#include "three_stage_h2d_huge.h"

extern int DirectDiscreteH2D(void** hostPinPtrs, void** devicePtrs, size_t* sizes, size_t count, uint32_t deviceId);
extern int AsyncPipelineH2D(void** hostPinPtrs, void** devicePtrs, size_t* sizes, size_t count, uint32_t deviceId, aclrtStream stream);
extern int FftsH2D(void** hostPinPtrs, void** devicePtrs, size_t* sizes, size_t count, uint32_t deviceId, aclrtStream stream, FftsDispatcherMinimal* dispatcher);
extern int ThreeStageH2D(void** hostRawPtrs, void** deviceDestPtrs, size_t* blobSizes, size_t objectCount, uint32_t deviceId, aclrtStream h2dStream, aclrtStream fftsStream, FftsDispatcherMinimal* dispatcher);

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
    constexpr size_t BLOBS_PER_OBJECT = 16;
    size_t objectCount = config.blobCount;
    size_t totalBlobCount = objectCount * BLOBS_PER_OBJECT;
    
    std::vector<void*> hostRawPtrs(totalBlobCount);
    std::vector<void*> devPtrs(totalBlobCount);
    std::vector<size_t> blobSizes(totalBlobCount, config.blobSize);
    
    for (size_t blobIdx = 0; blobIdx < totalBlobCount; blobIdx++) {
        hostRawPtrs[blobIdx] = std::malloc(config.blobSize);
        std::memset(hostRawPtrs[blobIdx], 'E' + blobIdx % 26, config.blobSize);
        aclrtMalloc(&devPtrs[blobIdx], config.blobSize, ACL_MEM_MALLOC_HUGE_FIRST);
    }
    
    aclrtStream h2dStream, fftsStream;
    aclrtCreateStream(&h2dStream);
    aclrtCreateStream(&fftsStream);
    
    FftsDispatcherMinimal dispatcher;
    dispatcher.Init();
    dispatcher.CreateFftsCtxs(2);
    
    auto h2hThreadPool = std::make_shared<SimpleThreadPool>(2);
    auto h2dThreadPool = std::make_shared<SimpleThreadPool>(1);
    dispatcher.SetH2hThreadPool(h2hThreadPool);
    dispatcher.SetH2dThreadPool(h2dThreadPool);
    
    auto start = std::chrono::high_resolution_clock::now();
    int ret = ThreeStageH2D(hostRawPtrs.data(), devPtrs.data(), blobSizes.data(), objectCount, 0, h2dStream, fftsStream, &dispatcher);
    auto end = std::chrono::high_resolution_clock::now();
    
    double seconds = std::chrono::duration<double>(end - start).count();
    size_t totalBytes = totalBlobCount * config.blobSize;
    double bandwidth = CalculateBandwidth(totalBytes, seconds);
    
    printf("  3-Stage: %.2f GB/s (%zu objects, %zu blobs/object)", bandwidth, objectCount, BLOBS_PER_OBJECT);
    if (ret != 0) printf(" (failed: %d)", ret);
    printf("\n");
    
    h2hThreadPool->Shutdown();
    h2dThreadPool->Shutdown();
    aclrtDestroyStream(h2dStream);
    aclrtDestroyStream(fftsStream);
    for (size_t blobIdx = 0; blobIdx < totalBlobCount; blobIdx++) {
        std::free(hostRawPtrs[blobIdx]);
        aclrtFree(devPtrs[blobIdx]);
    }
}

void BenchmarkThreeStageHuge(const TestConfig& config) {
    const size_t blobsPerObject = 3;
    size_t objectCount = (config.blobCount / blobsPerObject);
    if (objectCount == 0) objectCount = 1;
    size_t totalBlobs = objectCount * blobsPerObject;
    size_t objectSize = config.blobSize * blobsPerObject;
    
    std::vector<void*> hostPins(objectCount);
    std::vector<void*> devPtrs(totalBlobs);
    std::vector<size_t> blobSizes(totalBlobs, config.blobSize);
    
    for (size_t i = 0; i < objectCount; i++) {
        aclrtMallocHost(&hostPins[i], objectSize);
        std::memset(hostPins[i], 'I' + i, objectSize);
    }
    for (size_t i = 0; i < totalBlobs; i++) {
        aclrtMalloc(&devPtrs[i], config.blobSize, ACL_MEM_MALLOC_HUGE_FIRST);
    }
    
    std::vector<size_t> objectSizes(objectCount, objectSize);
    std::vector<size_t> blobCounts(objectCount, blobsPerObject);
    std::vector<size_t> blobOffsets(objectCount);
    for (size_t i = 0; i < objectCount; i++) {
        blobOffsets[i] = i * blobsPerObject;
    }
    
    aclrtStream h2dStream, fftsStream;
    aclrtCreateStream(&h2dStream);
    aclrtCreateStream(&fftsStream);
    
    FftsDispatcherMinimal dispatcher;
    dispatcher.Init();
    dispatcher.CreateFftsCtxs(2);
    
    auto h2hThreadPool = std::make_shared<SimpleThreadPool>(2);
    auto h2dThreadPool = std::make_shared<SimpleThreadPool>(1);
    dispatcher.SetH2hThreadPool(h2hThreadPool);
    dispatcher.SetH2dThreadPool(h2dThreadPool);
    
    auto start = std::chrono::high_resolution_clock::now();
    int ret = ThreeStageH2D_Huge(
        hostPins.data(), devPtrs.data(),
        objectSizes.data(), blobSizes.data(),
        blobCounts.data(), blobOffsets.data(),
        objectCount, 0, h2dStream, fftsStream, &dispatcher);
    auto end = std::chrono::high_resolution_clock::now();
    
    double seconds = std::chrono::duration<double>(end - start).count();
    size_t totalBytes = totalBlobs * config.blobSize;
    double bandwidth = CalculateBandwidth(totalBytes, seconds);
    
    printf("  3-Stage-H: %.2f GB/s (%zu objects, %zu blobs/object)", bandwidth, objectCount, blobsPerObject);
    if (ret != 0) printf(" (failed: %d)", ret);
    printf("\n");
    
    h2hThreadPool->Shutdown();
    h2dThreadPool->Shutdown();
    aclrtDestroyStream(h2dStream);
    aclrtDestroyStream(fftsStream);
    for (size_t i = 0; i < objectCount; i++) {
        aclrtFreeHost(hostPins[i]);
    }
    for (size_t i = 0; i < totalBlobs; i++) {
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
        BenchmarkThreeStageHuge(config);
    }
    
    printf("\n========================================\n");
    printf("  Benchmark Complete\n");
    printf("========================================\n");
    
    aclrtResetDevice(0);
    aclFinalize();
    
    return 0;
}