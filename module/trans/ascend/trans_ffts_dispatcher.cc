/**
 * MIT License
 *
 * Copyright (c) 2026 Mag1c.H
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * */
#include "trans_ffts_dispatcher.h"
#include <cstring>
#include <runtime/rt.h>
#include <vector>
#include "trans_assert_ascend.h"
#include "trans_thread_pool.h"

class FftsDispatcher {
    struct FftsCtx {
        std::vector<rtFftsPlusComCtx_t> ctxs{128};
        std::size_t index{0};
        std::size_t number{0};
    };
    struct PipeSlot {
        void* devPinBuffer;
        rtNotify_t toPinDone;
        rtNotify_t toDestDone;
    };
    struct Object {
        void* hostPinBuffer{nullptr};
        std::vector<void*> hBlobs;
        std::vector<void*> dBlobs;
        std::size_t blobSize;
    };
    struct TransIoCtx {
        std::vector<Object> objects;
        std::vector<std::size_t> tasks;
        std::mutex mtx;
        std::condition_variable cv;
        std::future<void> fftsFuture;
        std::vector<std::future<void>> h2hFutures;
        std::atomic_bool submitDone{false};
    };

    static constexpr std::size_t nSlotPerPipe = 2;
    static constexpr std::size_t nBlobPerObject = 16;
    static constexpr std::size_t maxObjectSize = 2 * 1024 * 1024;
    static constexpr std::size_t fftsMaxParallel = 8;
    std::size_t device_;
    FftsCtx ctxs_[nSlotPerPipe];
    FftsCtx* current_;
    std::unique_ptr<TransThreadPool> h2hTpool_;
    std::unique_ptr<TransThreadPool> fftsTpool_;
    aclrtStream h2dStream_;
    aclrtStream fftsStream_;
    PipeSlot slots_[nSlotPerPipe];
    TransIoCtx ioCtx_;

    void Reuse(std::size_t index)
    {
        ctxs_[index].index = 0;
        ctxs_[index].number = 0;
    }
    void SetCurrent(std::size_t index) { current_ = &ctxs_[index]; }
    void EnsureCapacity()
    {
        if (current_->index >= current_->ctxs.size()) {
            current_->ctxs.resize(current_->ctxs.size() * 2);
        }
    }
    std::size_t MemcpyAsync(void* src, void* dst, std::size_t size)
    {
        EnsureCapacity();
        auto ctx = static_cast<rtFftsPlusSdmaCtx_t*>((void*)&current_->ctxs[current_->index]);
        std::memset(ctx, 0, sizeof(*ctx));
        ctx->contextType = RT_CTX_TYPE_SDMA;
        ctx->threadDim = 1;
        ctx->sdmaSqeHeader = 0x1E70;  // SDMA_FP32_ATOMIC_MOVE_SQE
        const uint64_t lowMask = 0x00000000ffffffffULL;
        const uint64_t highMask = 0xffffffff00000000ULL;
        ctx->sourceAddressBaseL = reinterpret_cast<uint64_t>(src) & lowMask;
        ctx->sourceAddressBaseH = (reinterpret_cast<uint64_t>(src) & highMask) >> 32;
        ctx->destinationAddressBaseL = reinterpret_cast<uint64_t>(dst) & lowMask;
        ctx->destinationAddressBaseH = (reinterpret_cast<uint64_t>(dst) & highMask) >> 32;
        ctx->nonTailDataLength = size;
        ctx->tailDataLength = size;
        return current_->index++;
    }
    void AddTaskDependency(std::size_t predecessor, std::size_t successor)
    {
        auto& pred = current_->ctxs[predecessor];
        TRANS_ASSERT(pred.successorNum < RT_CTX_SUCCESSOR_NUM);
        pred.successorList[pred.successorNum] = successor;
        pred.successorNum++;
        auto& succ = current_->ctxs[successor];
        succ.predCntInit++;
        succ.predCnt++;
    }
    void Launch(std::size_t readyCount)
    {
        current_->number = current_->index;
        if (current_->number == 0) { return; }
        rtFftsPlusSqe_t sqe = {};
        sqe.fftsType = RT_FFTS_PLUS_TYPE;
        sqe.totalContextNum = current_->number;
        sqe.readyContextNum = readyCount;
        sqe.preloadContextNum = (readyCount <= 128) ? readyCount : 128;
        sqe.timeout = 0;
        sqe.subType = 0x5A;  // Communication task

        rtFftsPlusTaskInfo_t task = {};
        task.fftsPlusSqe = &sqe;
        task.descBuf = current_->ctxs.data();
        task.descBufLen = current_->number * sizeof(rtFftsPlusSdmaCtx_t);
        task.descAddrType = 0;
        task.argsHandleInfoNum = 0;
        task.argsHandleInfoPtr = nullptr;

        uintptr_t input[2];
        input[0] = reinterpret_cast<uintptr_t>(&task);
        input[1] = reinterpret_cast<uintptr_t>(fftsStream_);

        auto ret = rtGeneralCtrl(input, 2, RT_GNL_CTRL_TYPE_FFTS_PLUS);
        TRANS_ASSERT(ret == 0);
    }
    void ResetIoCtx()
    {
        for (auto& object : ioCtx_.objects) { ASCEND_ASSERT(aclrtFreeHost(object.hostPinBuffer)); }
        ioCtx_.objects.clear();
        ioCtx_.h2hFutures.clear();
        ioCtx_.submitDone.store(false);
    }
    void RunFftsLoop()
    {
        ioCtx_.fftsFuture = fftsTpool_->Submit([this] {
            ASCEND_ASSERT(aclrtSetDevice(device_));
            for (;;) {
                std::vector<std::size_t> tasks;
                {
                    std::unique_lock<std::mutex> lock(ioCtx_.mtx);
                    ioCtx_.cv.wait(
                        lock, [this] { return !ioCtx_.tasks.empty() || ioCtx_.submitDone.load(); });
                    if (ioCtx_.tasks.empty() && ioCtx_.submitDone.load()) { return; }
                    tasks.swap(ioCtx_.tasks);
                }
                for (auto& index : tasks) {
                    auto& object = ioCtx_.objects[index];
                    const auto blobCount = object.dBlobs.size();
                    const auto objectSize = object.blobSize * blobCount;
                    const auto slotIndex = index % nSlotPerPipe;
                    auto& slot = slots_[slotIndex];
                    TRANS_ASSERT(rtNotifyWait(slot.toDestDone, h2dStream_) == 0);
                    ASCEND_ASSERT(aclrtMemcpyAsync(slot.devPinBuffer, objectSize,
                                                   object.hostPinBuffer, objectSize,
                                                   ACL_MEMCPY_HOST_TO_DEVICE, h2dStream_));
                    TRANS_ASSERT(rtNotifyRecord(slot.toPinDone, h2dStream_) == 0);
                    TRANS_ASSERT(rtNotifyWait(slot.toPinDone, fftsStream_) == 0);
                    SetCurrent(slotIndex);
                    std::vector<std::size_t> lastTaskId(fftsMaxParallel, std::size_t(-1));
                    std::vector<std::size_t> fftsTaskIds(blobCount);
                    for (std::size_t n = 0; n < blobCount; n++) {
                        void* src = static_cast<uint8_t*>(slot.devPinBuffer) + object.blobSize * n;
                        fftsTaskIds[n] = MemcpyAsync(src, object.dBlobs[n], object.blobSize);
                        auto chainIdx = n % fftsMaxParallel;
                        if (lastTaskId[chainIdx] != std::size_t(-1)) {
                            AddTaskDependency(lastTaskId[chainIdx], fftsTaskIds[n]);
                        }
                        lastTaskId[chainIdx] = fftsTaskIds[n];
                    }
                    auto readyCount = static_cast<uint16_t>(std::min(blobCount, fftsMaxParallel));
                    Launch(readyCount);
                    Reuse(slotIndex);
                    TRANS_ASSERT(rtNotifyRecord(slot.toDestDone, fftsStream_) == 0);
                }
            }
        });
    }

public:
    FftsDispatcher(std::size_t device, std::size_t nH2HThread, std::size_t nH2DThread)
        : device_{device}
    {
        ASCEND_ASSERT(aclrtSetDevice(device));
        ASCEND_ASSERT(aclrtCreateStream(&h2dStream_));
        ASCEND_ASSERT(aclrtCreateStream(&fftsStream_));
        for (std::size_t i = 0; i < nSlotPerPipe; i++) {
            auto& slot = slots_[i];
            ASCEND_ASSERT(
                aclrtMalloc(&slot.devPinBuffer, maxObjectSize, ACL_MEM_MALLOC_HUGE_FIRST));
            TRANS_ASSERT(rtNotifyCreate(device, &slot.toPinDone) == 0);
            TRANS_ASSERT(rtNotifyCreate(device, &slot.toDestDone) == 0);
            TRANS_ASSERT(rtNotifyRecord(slot.toDestDone, fftsStream_) == 0);
        }
        h2hTpool_ = std::make_unique<TransThreadPool>(nH2HThread);
        fftsTpool_ = std::make_unique<TransThreadPool>(nH2DThread);
        ResetIoCtx();
    }
    ~FftsDispatcher()
    {
        ASCEND_ASSERT(aclrtSetDevice(device_));
        ASCEND_ASSERT(aclrtDestroyStream(h2dStream_));
        ASCEND_ASSERT(aclrtDestroyStream(fftsStream_));
        for (std::size_t i = 0; i < nSlotPerPipe; i++) {
            auto& slot = slots_[i];
            ASCEND_ASSERT(aclrtFree(slot.devPinBuffer));
            TRANS_ASSERT(rtNotifyDestroy(slot.toPinDone) == 0);
            TRANS_ASSERT(rtNotifyDestroy(slot.toDestDone) == 0);
        }
    }
    void H2DAsync(void** src, void** dst, std::size_t size, std::size_t number)
    {
        const auto nObject = (number + nBlobPerObject - 1) / nBlobPerObject;
        TRANS_ASSERT(nObject > 0);
        RunFftsLoop();
        // 挨个构建、处理所有的Object
        ioCtx_.objects.resize(nObject);
        for (std::size_t i = 0; i < nObject; i++) {
            // 构建Object，一个Object中包含多个Blob
            auto& object = ioCtx_.objects[i];
            const auto start = nBlobPerObject * i;
            const auto end = std::min(start + nBlobPerObject, number);
            for (std::size_t j = start; j < end; j++) {
                object.hBlobs.emplace_back(src[j]);
                object.dBlobs.emplace_back(dst[j]);
            }
            object.blobSize = size;
            ASCEND_ASSERT(aclrtMallocHost(&object.hostPinBuffer, size * object.hBlobs.size()));
            // 提交Object到H2H的线程池中
            ioCtx_.h2hFutures.emplace_back(h2hTpool_->Submit([this, index = i] {
                auto& object = ioCtx_.objects[index];
                // 将Object中的所有Blob合并到pinBuffer
                const auto nBlob = object.hBlobs.size();
                for (std::size_t i = 0; i < nBlob; i++) {
                    void* host = static_cast<uint8_t*>(object.hostPinBuffer) + object.blobSize * i;
                    std::memcpy(host, object.hBlobs[i], object.blobSize);
                }
                // 入队，唤醒FFTS线程
                {
                    std::unique_lock<std::mutex> lock(ioCtx_.mtx);
                    ioCtx_.tasks.emplace_back(index);
                    ioCtx_.cv.notify_one();
                }
            }));
        }
        ioCtx_.submitDone.store(true);
    }
    void Sync()
    {
        for (auto& fut : ioCtx_.h2hFutures) { fut.wait(); }
        ioCtx_.fftsFuture.wait();
        ASCEND_ASSERT(aclrtSynchronizeStream(h2dStream_));
        ASCEND_ASSERT(aclrtSynchronizeStream(fftsStream_));
        ResetIoCtx();
    }
};

FftsDispatcherHandle TransMakeFftsDispatcher(std::size_t device, std::size_t nH2HThread,
                                             std::size_t nH2DThread)
{
    return static_cast<FftsDispatcherHandle>(new FftsDispatcher(device, nH2HThread, nH2DThread));
}

void TransReleaseFftsDispatcher(FftsDispatcherHandle handle)
{
    if (!handle) { return; }
    delete static_cast<FftsDispatcher*>(handle);
}

void TransSubmitH2DIo2FftsDispatcher(void** src, void** dst, std::size_t size, std::size_t number,
                                     FftsDispatcherHandle handle)
{
    static_cast<FftsDispatcher*>(handle)->H2DAsync(src, dst, size, number);
}

void TransSyncFftsDispatcher(FftsDispatcherHandle handle)
{
    static_cast<FftsDispatcher*>(handle)->Sync();
}
