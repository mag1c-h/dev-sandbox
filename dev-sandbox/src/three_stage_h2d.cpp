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
#include <future>
#include <atomic>

extern int ThreeStageH2D(
    void** hostRawPtrs,
    void** deviceDestPtrs,
    size_t* sizes,
    size_t count,
    uint32_t deviceId,
    aclrtStream h2dStream,
    aclrtStream fftsStream,
    FftsDispatcherMinimal* dispatcher);

int ThreeStageH2D(
    void** hostRawPtrs,
    void** deviceDestPtrs,
    size_t* sizes,
    size_t count,
    uint32_t deviceId,
    aclrtStream h2dStream,
    aclrtStream fftsStream,
    FftsDispatcherMinimal* dispatcher)
{
    aclrtSetDevice(deviceId);
    
    if (count == 0) return 0;
    
    size_t totalSize = 0;
    std::vector<size_t> offsets(count);
    for (size_t i = 0; i < count; i++) {
        offsets[i] = totalSize;
        totalSize += sizes[i];
    }
    
    if (totalSize == 0) return 0;
    
    void* hostPinBuffer = nullptr;
    aclError aclRet = aclrtMallocHost(&hostPinBuffer, totalSize);
    if (aclRet != ACL_SUCCESS || hostPinBuffer == nullptr) {
        return aclRet;
    }
    
    void* devicePinBuffers[2] = {nullptr, nullptr};
    aclRet = aclrtMalloc(&devicePinBuffers[0], totalSize, ACL_MEM_MALLOC_HUGE_FIRST);
    if (aclRet != ACL_SUCCESS) {
        aclrtFreeHost(hostPinBuffer);
        return aclRet;
    }
    aclRet = aclrtMalloc(&devicePinBuffers[1], totalSize, ACL_MEM_MALLOC_HUGE_FIRST);
    if (aclRet != ACL_SUCCESS) {
        aclrtFreeHost(hostPinBuffer);
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
            aclrtFreeHost(hostPinBuffer);
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
            aclrtFreeHost(hostPinBuffer);
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
        aclrtFreeHost(hostPinBuffer);
        aclrtFree(devicePinBuffers[0]);
        aclrtFree(devicePinBuffers[1]);
        return -1;
    }
    
    std::atomic<bool> errorFlag{false};
    std::vector<std::future<void>> h2hFuts;
    
    std::atomic<size_t> finishCount{0};
    std::atomic<size_t> processedCount{0};
    std::atomic<bool> h2hAllDone{false};
    
    std::mutex queueMutex;
    std::condition_variable queueCv;
    H2DTaskQueue readyQueue;
    
    auto h2dFut = h2dThreadPool->Submit([&]() {
        while (true) {
            H2DTaskQueue tasks;
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
                
                size_t slot = task.index % 2;
                
                if (processedCount.load() >= 2) {
                    int rtRet = rtNotifyWait(toDestDone[slot], h2dStream);
                    if (rtRet != 0) {
                        errorFlag.store(true);
                        break;
                    }
                }
                
                aclRet = aclrtMemcpyAsync(
                    devicePinBuffers[slot], totalSize,
                    task.hostPinPtr, task.size,
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
                
                uint32_t taskId;
                int ret = dispatcher->MemcpyAsync(
                    task.deviceDestPtr,
                    devicePinBuffers[slot],
                    task.size,
                    &taskId);
                if (ret != 0) {
                    errorFlag.store(true);
                    break;
                }
                
                dispatcher->LaunchFftsTask(fftsStream, 1, slot);
                
                rtRet = rtNotifyRecord(toDestDone[slot], fftsStream);
                if (rtRet != 0) {
                    errorFlag.store(true);
                    break;
                }
                
                processedCount.fetch_add(1);
            }
        }
    });
    
    for (size_t i = 0; i < count; i++) {
        h2hFuts.emplace_back(h2hThreadPool->Submit([&, i]() {
            void* dst = static_cast<uint8_t*>(hostPinBuffer) + offsets[i];
            std::memcpy(dst, hostRawPtrs[i], sizes[i]);
            
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                H2DTaskInfo task;
                task.index = i;
                task.hostPinPtr = dst;
                task.size = sizes[i];
                task.deviceDestPtr = deviceDestPtrs[i];
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
    aclrtFreeHost(hostPinBuffer);
    aclrtFree(devicePinBuffers[0]);
    aclrtFree(devicePinBuffers[1]);
    
    return errorFlag.load() ? -1 : 0;
}