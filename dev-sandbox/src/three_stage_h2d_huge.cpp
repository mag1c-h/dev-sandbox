#include "three_stage_h2d_huge.h"
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
    FftsDispatcherMinimal* dispatcher)
{
    aclrtSetDevice(deviceId);
    
    if (objectCount == 0) return 0;
    
    size_t maxSize = 0;
    for (size_t i = 0; i < objectCount; i++) {
        maxSize = std::max(maxSize, objectSizes[i]);
    }
    
    if (maxSize == 0) return 0;
    
    size_t totalBlobCount = 0;
    for (size_t i = 0; i < objectCount; i++) {
        totalBlobCount += blobCounts[i];
    }
    
    std::vector<BufferMetaInfo> bufferMetas(objectCount);
    size_t blobOffsetAccum = 0;
    for (size_t i = 0; i < objectCount; i++) {
        bufferMetas[i].blobCount = blobCounts[i];
        bufferMetas[i].firstBlobOffset = blobOffsetAccum;
        bufferMetas[i].objectSize = objectSizes[i];
        blobOffsetAccum += blobCounts[i];
    }
    
    void* devicePinBuffers[FFTS_PIPELINE] = {nullptr, nullptr};
    aclError aclRet = aclrtMalloc(&devicePinBuffers[0], maxSize, ACL_MEM_MALLOC_HUGE_FIRST);
    if (aclRet != ACL_SUCCESS) return aclRet;
    
    aclRet = aclrtMalloc(&devicePinBuffers[1], maxSize, ACL_MEM_MALLOC_HUGE_FIRST);
    if (aclRet != ACL_SUCCESS) {
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
        aclrtFree(devicePinBuffers[0]);
        aclrtFree(devicePinBuffers[1]);
        return -1;
    }
    
    std::mutex queueMutex;
    std::condition_variable queueCv;
    PipelineH2DTasks pendingTasks;
    
    pendingTasks.destBuffers.resize(totalBlobCount);
    size_t destIdx = 0;
    for (size_t i = 0; i < objectCount; i++) {
        for (size_t n = 0; n < blobCounts[i]; n++) {
            size_t globalBlobIdx = blobOffsets[i] + n;
            pendingTasks.destBuffers[destIdx].ptr = deviceDestPtrs[globalBlobIdx];
            pendingTasks.destBuffers[destIdx].size = blobSizes[globalBlobIdx];
            destIdx++;
        }
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
                    devicePinBuffers[slot], maxSize,
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
    
    for (size_t objIdx = 0; objIdx < objectCount; objIdx++) {
        h2hFuts.emplace_back(h2hThreadPool->Submit([&, objIdx]() {
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                pendingTasks.srcBuffers.push_back(BufferView{ hostPinPtrs[objIdx], objectSizes[objIdx] });
                pendingTasks.bufferMetas.emplace_back(bufferMetas[objIdx]);
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
    aclrtFree(devicePinBuffers[0]);
    aclrtFree(devicePinBuffers[1]);
    
    return errorFlag.load() ? -1 : 0;
}