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

struct HugeH2DTaskInfo {
    size_t objectIndex;
    void* hostPinPtr;
    size_t objectSize;
    std::vector<void*> deviceDestPtrs;
    std::vector<size_t> blobSizes;
    size_t blobCount;
    size_t firstBlobOffset;
};

struct HugeH2DTaskQueue {
    std::vector<HugeH2DTaskInfo> tasks;
    bool IsEmpty() const { return tasks.empty(); }
    void Clear() { tasks.clear(); }
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
    
    void* devicePinBuffers[2] = {nullptr, nullptr};
    aclError aclRet = aclrtMalloc(&devicePinBuffers[0], maxSize, ACL_MEM_MALLOC_HUGE_FIRST);
    if (aclRet != ACL_SUCCESS) return aclRet;
    
    aclRet = aclrtMalloc(&devicePinBuffers[1], maxSize, ACL_MEM_MALLOC_HUGE_FIRST);
    if (aclRet != ACL_SUCCESS) {
        aclrtFree(devicePinBuffers[0]);
        return aclRet;
    }
    
    rtNotify_t toPinDone[2] = {nullptr, nullptr};
    rtNotify_t toDestDone[2] = {nullptr, nullptr};
    
    for (int i = 0; i < 2; i++) {
        int rtRet = rtNotifyCreate(static_cast<int32_t>(deviceId), &toPinDone[i]);
        if (rtRet != 0) {
            for (int j = 0; j < i; j++) {
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
            for (int j = 0; j < i; j++) {
                rtNotifyDestroy(toPinDone[j]);
                rtNotifyDestroy(toDestDone[j]);
            }
            aclrtFree(devicePinBuffers[0]);
            aclrtFree(devicePinBuffers[1]);
            return rtRet;
        }
    }
    
    dispatcher->CreateFftsCtxs(2);
    
    auto h2hThreadPool = dispatcher->GetH2hThreadPool();
    auto h2dThreadPool = dispatcher->GetH2dThreadPool();
    
    if (!h2hThreadPool || !h2dThreadPool) {
        for (int i = 0; i < 2; i++) {
            rtNotifyDestroy(toPinDone[i]);
            rtNotifyDestroy(toDestDone[i]);
        }
        aclrtFree(devicePinBuffers[0]);
        aclrtFree(devicePinBuffers[1]);
        return -1;
    }
    
    const size_t MAX_PARALLEL = 8;
    
    std::atomic<bool> errorFlag{false};
    std::vector<std::future<void>> h2hFuts;
    
    std::atomic<size_t> finishCount{0};
    std::atomic<size_t> processedCount{0};
    std::atomic<bool> h2hAllDone{false};
    
    std::mutex queueMutex;
    std::condition_variable queueCv;
    HugeH2DTaskQueue readyQueue;
    
    auto h2dFut = h2dThreadPool->Submit([&]() {
        while (true) {
            HugeH2DTaskQueue tasks;
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                queueCv.wait(lock, [&]() {
                    return !readyQueue.IsEmpty() || h2hAllDone.load();
                });
                
                if (readyQueue.IsEmpty() && h2hAllDone.load()) {
                    break;
                }
                
                std::swap(tasks, readyQueue);
            }
            
            for (const auto& task : tasks.tasks) {
                if (errorFlag.load()) break;
                
                size_t slot = task.objectIndex % 2;
                
                if (processedCount.load() >= 2) {
                    int rtRet = rtNotifyWait(toDestDone[slot], h2dStream);
                    if (rtRet != 0) {
                        errorFlag.store(true);
                        break;
                    }
                }
                
                aclRet = aclrtMemcpyAsync(
                    devicePinBuffers[slot], maxSize,
                    task.hostPinPtr, task.objectSize,
                    ACL_MEMCPY_HOST_TO_DEVICE, h2dStream);
                if (aclRet != ACL_SUCCESS) {
                    errorFlag.store(true);
                    break;
                }
                
                int rtRet = rtNotifyRecord(toPinDone[slot], h2dStream);
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
                std::vector<uint32_t> taskIds(task.blobCount);
                
                size_t offsetInPinBuffer = 0;
                
                for (size_t blobIdx = 0; blobIdx < task.blobCount; blobIdx++) {
                    size_t globalBlobIdx = task.firstBlobOffset + blobIdx;
                    void* src = static_cast<uint8_t*>(devicePinBuffers[slot]) + offsetInPinBuffer;
                    
                    int ret = dispatcher->MemcpyAsync(
                        task.deviceDestPtrs[globalBlobIdx],
                        src,
                        task.blobSizes[globalBlobIdx],
                        &taskIds[blobIdx]);
                    if (ret != 0) {
                        errorFlag.store(true);
                        break;
                    }
                    
                    size_t chainIdx = blobIdx % MAX_PARALLEL;
                    if (lastTaskId[chainIdx] >= 0) {
                        dispatcher->AddTaskDependency(lastTaskId[chainIdx], taskIds[blobIdx]);
                    }
                    lastTaskId[chainIdx] = taskIds[blobIdx];
                    
                    offsetInPinBuffer += task.blobSizes[globalBlobIdx];
                }
                
                if (errorFlag.load()) break;
                
                uint16_t readyCount = std::min(static_cast<size_t>(task.blobCount), MAX_PARALLEL);
                dispatcher->LaunchFftsTask(fftsStream, readyCount, slot);
                
                dispatcher->ReuseCtx(slot);
                
                rtRet = rtNotifyRecord(toDestDone[slot], fftsStream);
                if (rtRet != 0) {
                    errorFlag.store(true);
                    break;
                }
                
                processedCount.fetch_add(1);
            }
        }
    });
    
    for (size_t objIdx = 0; objIdx < objectCount; objIdx++) {
        h2hFuts.emplace_back(h2hThreadPool->Submit([&, objIdx]() {
            size_t blobStartIdx = blobOffsets[objIdx];
            size_t blobCount = blobCounts[objIdx];
            
            std::vector<void*> deviceDestPtrsForObj;
            std::vector<size_t> blobSizesForObj;
            
            for (size_t blobIdx = 0; blobIdx < blobCount; blobIdx++) {
                size_t globalBlobIdx = blobStartIdx + blobIdx;
                deviceDestPtrsForObj.push_back(deviceDestPtrs[globalBlobIdx]);
                blobSizesForObj.push_back(blobSizes[globalBlobIdx]);
            }
            
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                HugeH2DTaskInfo task;
                task.objectIndex = objIdx;
                task.hostPinPtr = hostPinPtrs[objIdx];
                task.objectSize = objectSizes[objIdx];
                task.deviceDestPtrs = deviceDestPtrsForObj;
                task.blobSizes = blobSizesForObj;
                task.blobCount = blobCount;
                task.firstBlobOffset = blobStartIdx;
                readyQueue.tasks.push_back(task);
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
    
    for (int i = 0; i < 2; i++) {
        rtNotifyDestroy(toPinDone[i]);
        rtNotifyDestroy(toDestDone[i]);
    }
    aclrtFree(devicePinBuffers[0]);
    aclrtFree(devicePinBuffers[1]);
    
    return errorFlag.load() ? -1 : 0;
}