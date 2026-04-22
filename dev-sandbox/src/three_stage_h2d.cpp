#include "ffts_dispatcher_minimal.h"
#include "simple_thread_pool.h"

#ifdef USE_NPU
#include <acl/acl.h>
#include <runtime/dev.h>
#else
#include <cstdint>
typedef int aclError;
constexpr aclError ACL_SUCCESS = 0;
constexpr int ACL_MEMCPY_HOST_TO_DEVICE = 1;
constexpr int ACL_MEM_MALLOC_HUGE_FIRST = 0;
typedef void* aclrtStream;
typedef void* rtNotify_t;
inline aclError aclrtSetDevice(uint32_t) { return ACL_SUCCESS; }
inline aclError aclrtMallocHost(void**, size_t) { return ACL_SUCCESS; }
inline void aclrtFreeHost(void*) {}
inline aclError aclrtMalloc(void**, size_t, int) { return ACL_SUCCESS; }
inline void aclrtFree(void*) {}
inline aclError aclrtMemcpyAsync(void*, uint64_t, void*, uint64_t, int, aclrtStream) { return ACL_SUCCESS; }
inline aclError aclrtSynchronizeStream(aclrtStream) { return ACL_SUCCESS; }
inline int rtNotifyCreate(int32_t, rtNotify_t*) { return 0; }
inline int rtNotifyDestroy(rtNotify_t) { return 0; }
inline int rtNotifyWait(rtNotify_t, aclrtStream) { return 0; }
inline int rtNotifyRecord(rtNotify_t, aclrtStream) { return 0; }
#endif

#include <vector>
#include <cstring>
#include <algorithm>
#include <future>
#include <atomic>
#include <mutex>
#include <condition_variable>

constexpr size_t FFTS_PIPELINE = 2;
constexpr size_t BLOBS_PER_OBJECT = 16;

struct BufferView {
    void* ptr;
    size_t size;
};

struct BufferMetaInfo {
    size_t blobCount;
    size_t firstBlobOffset;
    size_t objectSize;
};

struct PipelineH2DTasks {
    std::vector<BufferView> srcBuffers;
    std::vector<BufferView> destBuffers;
    std::vector<BufferMetaInfo> bufferMetas;
    bool IsEmpty() const { return srcBuffers.empty(); }
    void Clear() {
        srcBuffers.clear();
        bufferMetas.clear();
    }
};

extern int ThreeStageH2D(
    void** hostRawPtrs,
    void** deviceDestPtrs,
    size_t* blobSizes,
    size_t objectCount,
    uint32_t deviceId,
    aclrtStream h2dStream,
    aclrtStream fftsStream,
    FftsDispatcherMinimal* dispatcher);

int ThreeStageH2D(
    void** hostRawPtrs,
    void** deviceDestPtrs,
    size_t* blobSizes,
    size_t objectCount,
    uint32_t deviceId,
    aclrtStream h2dStream,
    aclrtStream fftsStream,
    FftsDispatcherMinimal* dispatcher)
{
    aclrtSetDevice(deviceId);
    
    if (objectCount == 0) return 0;
    
    size_t totalBlobCount = objectCount * BLOBS_PER_OBJECT;
    
    size_t maxObjectSize = 0;
    std::vector<BufferMetaInfo> bufferMetas(objectCount);
    for (size_t i = 0; i < objectCount; i++) {
        bufferMetas[i].blobCount = BLOBS_PER_OBJECT;
        bufferMetas[i].firstBlobOffset = i * BLOBS_PER_OBJECT;
        size_t objectSize = 0;
        for (size_t n = 0; n < BLOBS_PER_OBJECT; n++) {
            size_t blobIdx = i * BLOBS_PER_OBJECT + n;
            objectSize += blobSizes[blobIdx];
        }
        bufferMetas[i].objectSize = objectSize;
        maxObjectSize = std::max(maxObjectSize, objectSize);
    }
    
    if (maxObjectSize == 0) return 0;
    
    std::vector<void*> hostPinBuffers(objectCount, nullptr);
    for (size_t i = 0; i < objectCount; i++) {
        aclError aclRet = aclrtMallocHost(&hostPinBuffers[i], bufferMetas[i].objectSize);
        if (aclRet != ACL_SUCCESS || hostPinBuffers[i] == nullptr) {
            for (size_t j = 0; j < i; j++) {
                aclrtFreeHost(hostPinBuffers[j]);
            }
            return aclRet;
        }
    }
    
    void* devicePinBuffers[FFTS_PIPELINE] = {nullptr, nullptr};
    aclError aclRet = aclrtMalloc(&devicePinBuffers[0], maxObjectSize, ACL_MEM_MALLOC_HUGE_FIRST);
    if (aclRet != ACL_SUCCESS) {
        for (size_t i = 0; i < objectCount; i++) {
            aclrtFreeHost(hostPinBuffers[i]);
        }
        return aclRet;
    }
    
    aclRet = aclrtMalloc(&devicePinBuffers[1], maxObjectSize, ACL_MEM_MALLOC_HUGE_FIRST);
    if (aclRet != ACL_SUCCESS) {
        for (size_t i = 0; i < objectCount; i++) {
            aclrtFreeHost(hostPinBuffers[i]);
        }
        aclrtFree(devicePinBuffers[0]);
        return aclRet;
    }
    
    rtNotify_t toPinDone[FFTS_PIPELINE] = {nullptr, nullptr};
    rtNotify_t toDestDone[FFTS_PIPELINE] = {nullptr, nullptr};
    
    for (size_t i = 0; i < FFTS_PIPELINE; i++) {
        int rtRet = rtNotifyCreate(static_cast<int32_t>(deviceId), &toPinDone[i]);
        if (rtRet != 0) {
            for (size_t j = 0; j < i; j++) {
                rtNotifyDestroy(toPinDone[j]);
                rtNotifyDestroy(toDestDone[j]);
            }
            for (size_t k = 0; k < objectCount; k++) {
                aclrtFreeHost(hostPinBuffers[k]);
            }
            aclrtFree(devicePinBuffers[0]);
            aclrtFree(devicePinBuffers[1]);
            return rtRet;
        }
        rtRet = rtNotifyCreate(static_cast<int32_t>(deviceId), &toDestDone[i]);
        if (rtRet != 0) {
            rtNotifyDestroy(toPinDone[i]);
            for (size_t j = 0; j < i; j++) {
                rtNotifyDestroy(toPinDone[j]);
                rtNotifyDestroy(toDestDone[j]);
            }
            for (size_t k = 0; k < objectCount; k++) {
                aclrtFreeHost(hostPinBuffers[k]);
            }
            aclrtFree(devicePinBuffers[0]);
            aclrtFree(devicePinBuffers[1]);
            return rtRet;
        }
    }
    
    for (size_t i = 0; i < FFTS_PIPELINE; i++) {
        int rtRet = rtNotifyRecord(toDestDone[i], h2dStream);
        if (rtRet != 0) {
            for (size_t j = 0; j < FFTS_PIPELINE; j++) {
                rtNotifyDestroy(toPinDone[j]);
                rtNotifyDestroy(toDestDone[j]);
            }
            for (size_t k = 0; k < objectCount; k++) {
                aclrtFreeHost(hostPinBuffers[k]);
            }
            aclrtFree(devicePinBuffers[0]);
            aclrtFree(devicePinBuffers[1]);
            return rtRet;
        }
    }
    
    dispatcher->CreateFftsCtxs(FFTS_PIPELINE);
    
    auto h2hThreadPool = dispatcher->GetH2hThreadPool();
    auto h2dThreadPool = dispatcher->GetH2dThreadPool();
    
    if (!h2hThreadPool || !h2dThreadPool) {
        for (size_t i = 0; i < FFTS_PIPELINE; i++) {
            rtNotifyDestroy(toPinDone[i]);
            rtNotifyDestroy(toDestDone[i]);
        }
        for (size_t i = 0; i < objectCount; i++) {
            aclrtFreeHost(hostPinBuffers[i]);
        }
        aclrtFree(devicePinBuffers[0]);
        aclrtFree(devicePinBuffers[1]);
        return -1;
    }
    
    std::mutex queueMutex;
    std::condition_variable queueCv;
    PipelineH2DTasks pendingTasks;
    
    pendingTasks.destBuffers.resize(totalBlobCount);
    for (size_t blobIdx = 0; blobIdx < totalBlobCount; blobIdx++) {
        pendingTasks.destBuffers[blobIdx].ptr = deviceDestPtrs[blobIdx];
        pendingTasks.destBuffers[blobIdx].size = blobSizes[blobIdx];
    }
    
    std::atomic<size_t> finishCount{0};
    std::atomic<bool> h2hAllDone{false};
    std::atomic<bool> errorFlag{false};
    std::vector<std::future<void>> h2hFuts;
    
    auto h2dFut = h2dThreadPool->Submit([&]() {
        size_t submitCount = 0;
        constexpr size_t MAX_PARALLEL = 8;
        
        while (true) {
            PipelineH2DTasks tasks;
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                queueCv.wait(lock, [&]() {
                    return !pendingTasks.IsEmpty() || h2hAllDone.load();
                });
                
                if (pendingTasks.IsEmpty() && h2hAllDone.load()) {
                    break;
                }
                
                std::swap(tasks.srcBuffers, pendingTasks.srcBuffers);
                std::swap(tasks.bufferMetas, pendingTasks.bufferMetas);
                pendingTasks.Clear();
            }
            
            for (size_t i = 0; i < tasks.srcBuffers.size(); i++) {
                if (errorFlag.load()) break;
                
                size_t slot = submitCount % FFTS_PIPELINE;
                submitCount++;
                
                void* hostPinBuffer = tasks.srcBuffers[i].ptr;
                size_t objectSize = tasks.bufferMetas[i].objectSize;
                size_t blobCount = tasks.bufferMetas[i].blobCount;
                size_t firstBlobOffset = tasks.bufferMetas[i].firstBlobOffset;
                
                int rtRet = rtNotifyWait(toDestDone[slot], h2dStream);
                if (rtRet != 0) {
                    errorFlag.store(true);
                    break;
                }
                
                aclRet = aclrtMemcpyAsync(
                    devicePinBuffers[slot], maxObjectSize,
                    hostPinBuffer, objectSize,
                    ACL_MEMCPY_HOST_TO_DEVICE, h2dStream);
                if (aclRet != ACL_SUCCESS) {
                    errorFlag.store(true);
                    break;
                }
                
                rtRet = rtNotifyRecord(toPinDone[slot], h2dStream);
                if (rtRet != 0) {
                    errorFlag.store(true);
                    break;
                }
                
                rtRet = rtNotifyWait(toPinDone[slot], fftsStream);
                if (rtRet != 0) {
                    errorFlag.store(true);
                    break;
                }
                
                dispatcher->SetFftsCtx(slot);
                dispatcher->ReuseCtx(slot);
                
                std::vector<int32_t> lastTaskId(MAX_PARALLEL, -1);
                std::vector<uint32_t> fftsTaskIds(blobCount);
                size_t offsetInDevicePinBuffer = 0;
                
                for (size_t n = 0; n < blobCount; n++) {
                    size_t blobIndex = firstBlobOffset + n;
                    void* src = static_cast<uint8_t*>(devicePinBuffers[slot]) + offsetInDevicePinBuffer;
                    void* dest = pendingTasks.destBuffers[blobIndex].ptr;
                    size_t blobSize = pendingTasks.destBuffers[blobIndex].size;
                    
                    int ret = dispatcher->MemcpyAsync(dest, src, blobSize, &fftsTaskIds[n]);
                    if (ret != 0) {
                        errorFlag.store(true);
                        break;
                    }
                    
                    size_t chainIdx = n % MAX_PARALLEL;
                    if (lastTaskId[chainIdx] >= 0) {
                        dispatcher->AddTaskDependency(lastTaskId[chainIdx], fftsTaskIds[n]);
                    }
                    lastTaskId[chainIdx] = fftsTaskIds[n];
                    
                    offsetInDevicePinBuffer += blobSize;
                }
                
                if (errorFlag.load()) break;
                
                uint16_t readyCount = static_cast<uint16_t>(std::min(blobCount, MAX_PARALLEL));
                dispatcher->LaunchFftsTask(fftsStream, readyCount, slot);
                dispatcher->ReuseCtx(slot);
                
                rtRet = rtNotifyRecord(toDestDone[slot], fftsStream);
                if (rtRet != 0) {
                    errorFlag.store(true);
                    break;
                }
            }
        }
    });
    
    for (size_t i = 0; i < objectCount; i++) {
        h2hFuts.emplace_back(h2hThreadPool->Submit([&, i]() {
            size_t blobStartIdx = i * BLOBS_PER_OBJECT;
            size_t offsetInHostPinBuffer = 0;
            
            for (size_t n = 0; n < BLOBS_PER_OBJECT; n++) {
                size_t blobIdx = blobStartIdx + n;
                void* dst = static_cast<uint8_t*>(hostPinBuffers[i]) + offsetInHostPinBuffer;
                std::memcpy(dst, hostRawPtrs[blobIdx], blobSizes[blobIdx]);
                offsetInHostPinBuffer += blobSizes[blobIdx];
            }
            
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                pendingTasks.srcBuffers.push_back(BufferView{ hostPinBuffers[i], bufferMetas[i].objectSize });
                pendingTasks.bufferMetas.emplace_back(bufferMetas[i]);
                finishCount.fetch_add(1);
            }
            queueCv.notify_one();
        }));
    }
    
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        h2hAllDone.store(true);
    }
    queueCv.notify_one();
    
    for (auto& f : h2hFuts) {
        f.wait();
    }
    
    h2dFut.wait();
    
    aclrtSynchronizeStream(h2dStream);
    aclrtSynchronizeStream(fftsStream);
    
    for (size_t i = 0; i < FFTS_PIPELINE; i++) {
        rtNotifyDestroy(toPinDone[i]);
        rtNotifyDestroy(toDestDone[i]);
    }
    for (size_t i = 0; i < objectCount; i++) {
        aclrtFreeHost(hostPinBuffers[i]);
    }
    aclrtFree(devicePinBuffers[0]);
    aclrtFree(devicePinBuffers[1]);
    
    return errorFlag.load() ? -1 : 0;
}