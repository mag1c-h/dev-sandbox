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
#ifndef TRANS_TEMPLATE_ASCEND_H
#define TRANS_TEMPLATE_ASCEND_H

#include "trans_assert_ascend.h"
#include "trans_dma_dispatcher_ascend.h"
#include "trans_stopwatch.h"
#include "trans_template.h"

class TransStreamTemplate : public TransTemplate {
protected:
    struct TransTask {
        std::size_t device;
        aclrtStream stream;
        aclrtEvent finish;
        std::size_t size;
        std::vector<void*> src;
        std::vector<void*> dst;
    };
    std::vector<TransTask> tasks_;

    void OnTransPre(const std::vector<const TransBuffer*>& srcBuffers,
                    const std::vector<const TransBuffer*>& dstBuffers) override
    {
        tasks_.clear();
        TRANS_ASSERT(srcBuffers.size() == dstBuffers.size());
        for (std::size_t i = 0; i < srcBuffers.size(); i++) {
            auto& srcBuffer = *srcBuffers[i];
            auto& dstBuffer = *dstBuffers[i];
            TRANS_ASSERT(srcBuffer.Number() == dstBuffer.Number());
            TRANS_ASSERT(srcBuffer.Size() == dstBuffer.Size());
            TransTask task;
            task.device = AffinityDeviceId(srcBuffer, dstBuffer);
            ASCEND_ASSERT(aclrtSetDevice(task.device));
            ASCEND_ASSERT(aclrtCreateStream(&task.stream));
            ASCEND_ASSERT(aclrtCreateEvent(&task.finish));
            task.size = srcBuffer.Size();
            for (std::size_t j = 0; j < srcBuffer.Number(); j++) {
                task.src.push_back(srcBuffer[j]);
                task.dst.push_back(dstBuffer[j]);
            }
            tasks_.push_back(std::move(task));
        }
    }
    virtual void OnTransSubmit(TransTask& task) const = 0;
    std::pair<std::size_t, std::size_t> OnTrans() override
    {
        aclrtEvent start;
        aclrtEvent end;
        auto& first = tasks_[0];
        ASCEND_ASSERT(aclrtSetDevice(first.device));
        ASCEND_ASSERT(aclrtCreateEvent(&start));
        ASCEND_ASSERT(aclrtCreateEvent(&end));

        TransStopwatch watch;
        ASCEND_ASSERT(aclrtRecordEvent(start, first.stream));
        for (std::size_t i = 1; i < tasks_.size(); i++) {
            ASCEND_ASSERT(aclrtSetDevice(tasks_[i].device));
            ASCEND_ASSERT(aclrtStreamWaitEvent(tasks_[i].stream, start));
        }
        for (auto& task : tasks_) { OnTransSubmit(task); }
        for (std::size_t i = 1; i < tasks_.size(); i++) {
            ASCEND_ASSERT(aclrtSetDevice(first.device));
            ASCEND_ASSERT(aclrtStreamWaitEvent(first.stream, tasks_[i].finish));
        }
        ASCEND_ASSERT(aclrtRecordEvent(end, first.stream));
        auto costSubmit = watch.Elapse();

        ASCEND_ASSERT(aclrtSetDevice(first.device));
        ASCEND_ASSERT(aclrtSynchronizeStream(first.stream));
        auto costMs = 0.0f;
        ASCEND_ASSERT(aclrtEventElapsedTime(&costMs, start, end));
        ASCEND_ASSERT(aclrtDestroyEvent(start));
        ASCEND_ASSERT(aclrtDestroyEvent(end));
        return {costMs * 1e3, costSubmit};
    }
    void OnTransPost() override
    {
        for (auto& task : tasks_) {
            ASCEND_ASSERT(aclrtSetDevice(task.device));
            ASCEND_ASSERT(aclrtDestroyEvent(task.finish));
            ASCEND_ASSERT(aclrtDestroyStream(task.stream));
        }
        tasks_.clear();
    }

public:
    TransStreamTemplate(std::size_t iterations, bool affinitySrc)
        : TransTemplate{iterations, affinitySrc}
    {
    }
};

class TransH2DCETemplate : public TransStreamTemplate {
protected:
    void OnTransSubmit(TransTask& task) const override
    {
        ASCEND_ASSERT(aclrtSetDevice(task.device));
        for (std::size_t i = 0; i < task.src.size(); i++) {
            ASCEND_ASSERT(aclrtMemcpyAsync(task.dst[i], task.size, task.src[i], task.size,
                                           ACL_MEMCPY_HOST_TO_DEVICE, task.stream));
        }
        ASCEND_ASSERT(aclrtRecordEvent(task.finish, task.stream));
    }

public:
    TransH2DCETemplate(std::size_t iterations, bool affinitySrc)
        : TransStreamTemplate{iterations, affinitySrc}
    {
    }
    std::string Name() const override { return "ce"; }
};

class TransH2DBatchCETemplate : public TransStreamTemplate {
protected:
    std::size_t device_;

    void OnTransSubmit(TransTask& task) const override
    {
        ASCEND_ASSERT(aclrtSetDevice(task.device));
        aclrtMemcpyBatchAttr attr;
        std::memset(&attr, 0, sizeof(attr));
        attr.srcLoc.type = ACL_MEM_LOCATION_TYPE_HOST;
        attr.dstLoc.type = ACL_MEM_LOCATION_TYPE_DEVICE;
        attr.dstLoc.id = device_;
        std::vector<aclrtMemcpyBatchAttr> attrArray{attr};
        std::vector<size_t> attrIdxArray(task.src.size(), 0);
        std::vector<size_t> sizeArray(task.src.size(), task.size);
        size_t failureIdx = 0;
        ASCEND_ASSERT(aclrtMemcpyBatchAsync(const_cast<void**>(task.dst.data()), sizeArray.data(),
                                            const_cast<void**>(task.src.data()), sizeArray.data(),
                                            task.src.size(), attrArray.data(), attrIdxArray.data(),
                                            attrArray.size(), &failureIdx, task.stream));
        ASCEND_ASSERT(aclrtRecordEvent(task.finish, task.stream));
    }

public:
    TransH2DBatchCETemplate(std::size_t device, std::size_t iterations, bool affinitySrc)
        : TransStreamTemplate{iterations, affinitySrc}, device_{device}
    {
    }
    std::string Name() const override { return "batch-ce"; }
};

class TransD2HCETemplate : public TransStreamTemplate {
protected:
    void OnTransSubmit(TransTask& task) const override
    {
        ASCEND_ASSERT(aclrtSetDevice(task.device));
        for (std::size_t i = 0; i < task.src.size(); i++) {
            ASCEND_ASSERT(aclrtMemcpyAsync(task.dst[i], task.size, task.src[i], task.size,
                                           ACL_MEMCPY_DEVICE_TO_HOST, task.stream));
        }
        ASCEND_ASSERT(aclrtRecordEvent(task.finish, task.stream));
    }

public:
    TransD2HCETemplate(std::size_t iterations, bool affinitySrc)
        : TransStreamTemplate{iterations, affinitySrc}
    {
    }
    std::string Name() const override { return "ce"; }
};

class TransD2HBatchCETemplate : public TransStreamTemplate {
protected:
    std::size_t device_;

    void OnTransSubmit(TransTask& task) const override
    {
        ASCEND_ASSERT(aclrtSetDevice(task.device));
        aclrtMemcpyBatchAttr attr;
        std::memset(&attr, 0, sizeof(attr));
        attr.srcLoc.type = ACL_MEM_LOCATION_TYPE_DEVICE;
        attr.srcLoc.id = device_;
        attr.dstLoc.type = ACL_MEM_LOCATION_TYPE_HOST;
        std::vector<aclrtMemcpyBatchAttr> attrArray{attr};
        std::vector<size_t> attrIdxArray(task.src.size(), 0);
        std::vector<size_t> sizeArray(task.src.size(), task.size);
        size_t failureIdx = 0;
        ASCEND_ASSERT(aclrtMemcpyBatchAsync(const_cast<void**>(task.dst.data()), sizeArray.data(),
                                            const_cast<void**>(task.src.data()), sizeArray.data(),
                                            task.src.size(), attrArray.data(), attrIdxArray.data(),
                                            attrArray.size(), &failureIdx, task.stream));
        ASCEND_ASSERT(aclrtRecordEvent(task.finish, task.stream));
    }

public:
    TransD2HBatchCETemplate(std::size_t device, std::size_t iterations, bool affinitySrc)
        : TransStreamTemplate{iterations, affinitySrc}, device_{device}
    {
    }
    std::string Name() const override { return "batch-ce"; }
};

class TransMultiStreamTemplate : public TransStreamTemplate {
protected:
    std::size_t streamCount_;
    void OnTransPre(const std::vector<const TransBuffer*>& srcBuffers,
                    const std::vector<const TransBuffer*>& dstBuffers) override
    {
        tasks_.clear();
        TRANS_ASSERT(srcBuffers.size() == dstBuffers.size());
        for (std::size_t i = 0; i < srcBuffers.size(); i++) {
            auto& srcBuffer = *srcBuffers[i];
            auto& dstBuffer = *dstBuffers[i];
            TRANS_ASSERT(srcBuffer.Number() == dstBuffer.Number());
            TRANS_ASSERT(srcBuffer.Size() == dstBuffer.Size());
            auto bufferCount = srcBuffer.Number();
            auto base = bufferCount / streamCount_;
            auto remainder = bufferCount % streamCount_;
            auto device = AffinityDeviceId(srcBuffer, dstBuffer);
            ASCEND_ASSERT(aclrtSetDevice(device));
            std::size_t offset = 0;
            for (std::size_t s = 0; s < streamCount_; s++) {
                size_t count = base + (s < remainder ? 1 : 0);
                if (count == 0) { continue; }
                TransTask task;
                task.device = device;
                task.size = srcBuffer.Size();
                ASCEND_ASSERT(aclrtCreateStream(&task.stream));
                ASCEND_ASSERT(aclrtCreateEvent(&task.finish));
                for (size_t j = 0; j < count; j++) {
                    task.src.push_back(srcBuffer[offset + j]);
                    task.dst.push_back(dstBuffer[offset + j]);
                }
                tasks_.push_back(std::move(task));
                offset += count;
            }
        }
    }

public:
    TransMultiStreamTemplate(std::size_t streamCount, std::size_t iterations, bool affinitySrc)
        : TransStreamTemplate{iterations, affinitySrc}, streamCount_(streamCount)
    {
    }
    std::string Name() const override { return std::to_string(streamCount_) + "-stream"; }
};

class TransH2DMultiStreamTemplate : public TransMultiStreamTemplate {
protected:
    void OnTransSubmit(TransTask& task) const override
    {
        ASCEND_ASSERT(aclrtSetDevice(task.device));
        for (std::size_t i = 0; i < task.src.size(); i++) {
            ASCEND_ASSERT(aclrtMemcpyAsync(task.dst[i], task.size, task.src[i], task.size,
                                           ACL_MEMCPY_HOST_TO_DEVICE, task.stream));
        }
        ASCEND_ASSERT(aclrtRecordEvent(task.finish, task.stream));
    }

public:
    TransH2DMultiStreamTemplate(std::size_t streamCount, std::size_t iterations, bool affinitySrc)
        : TransMultiStreamTemplate{streamCount, iterations, affinitySrc}
    {
    }
};

class TransD2HMultiStreamTemplate : public TransMultiStreamTemplate {
protected:
    void OnTransSubmit(TransTask& task) const override
    {
        ASCEND_ASSERT(aclrtSetDevice(task.device));
        for (std::size_t i = 0; i < task.src.size(); i++) {
            ASCEND_ASSERT(aclrtMemcpyAsync(task.dst[i], task.size, task.src[i], task.size,
                                           ACL_MEMCPY_DEVICE_TO_HOST, task.stream));
        }
        ASCEND_ASSERT(aclrtRecordEvent(task.finish, task.stream));
    }

public:
    TransD2HMultiStreamTemplate(std::size_t streamCount, std::size_t iterations, bool affinitySrc)
        : TransMultiStreamTemplate{streamCount, iterations, affinitySrc}
    {
    }
};

class TransH2DDmaTemplate : public TransTemplate {
protected:
    static constexpr std::size_t DMA_PARALLEL = 8;
    static constexpr std::size_t MAX_NBLOB_PER_OBJ = DMA_PARALLEL * 2;
    static constexpr std::size_t PIPE_SLOT_NUM = 2;
    struct Object {
        std::vector<void*> blobs;
        std::size_t size;
        void* pinBuffer;
    };
    std::size_t device_;
    std::size_t size_;
    std::unique_ptr<TransDmaDispatcher> dispatcher_;
    aclrtStream h2dStream_;
    aclrtStream dmaStream_;
    void* deviceBuffer_[PIPE_SLOT_NUM];
    rtNotify_t toPinDone_[PIPE_SLOT_NUM];
    rtNotify_t toDestDone_[PIPE_SLOT_NUM];
    std::vector<Object> objects_;

    void OnTransPre(const std::vector<const TransBuffer*>& srcBuffers,
                    const std::vector<const TransBuffer*>& dstBuffers) override
    {
        ASCEND_ASSERT(aclrtSetDevice(device_));
        for (std::size_t i = 0; i < srcBuffers.size(); i++) {
            auto& srcBuffer = *(srcBuffers[i]);
            auto& dstBuffer = *(dstBuffers[i]);
            std::size_t offset = 0;
            while (offset < srcBuffer.Number()) {
                auto left = srcBuffer.Number() - offset;
                auto nBlob = std::min(left, MAX_NBLOB_PER_OBJ);
                Object obj;
                obj.size = srcBuffer.Size();
                ASCEND_ASSERT(aclrtMallocHost(&obj.pinBuffer, obj.size * nBlob));
                for (std::size_t j = 0; j < nBlob; j++, offset++) {
                    std::memcpy(static_cast<char*>(obj.pinBuffer) + obj.size * j, srcBuffer[offset],
                                obj.size);
                    obj.blobs[j] = dstBuffer[offset];
                }
                objects_.push_back(std::move(obj));
            }
        }
    }
    std::pair<size_t, size_t> OnTrans() override
    {
        TransStopwatch submit, trans;
        ASCEND_ASSERT(aclrtSetDevice(device_));
        for (std::size_t i = 0; i < PIPE_SLOT_NUM; i++) {
            ASCEND_ASSERT(rtNotifyRecord(toDestDone_[i], dmaStream_));
        }
        const auto objNumber = objects_.size();
        for (std::size_t objIdx = 0; objIdx < objNumber; objIdx++) {
            const auto& obj = objects_[objIdx];
            const auto slot = objIdx % PIPE_SLOT_NUM;
            const auto objTotalSize = obj.size * obj.blobs.size();
            ASCEND_ASSERT(rtNotifyWait(toDestDone_[slot], h2dStream_));
            ASCEND_ASSERT(aclrtMemcpyAsync(deviceBuffer_[slot], objTotalSize, obj.pinBuffer,
                                           objTotalSize, ACL_MEMCPY_HOST_TO_DEVICE, h2dStream_));
            ASCEND_ASSERT(rtNotifyRecord(toPinDone_[slot], h2dStream_));
            ASCEND_ASSERT(rtNotifyWait(toPinDone_[slot], dmaStream_));
            dispatcher_->ReuseCtx(0);
            std::vector<std::size_t> lastTaskId(DMA_PARALLEL, std::size_t(-1));
            std::vector<std::size_t> taskIds(obj.blobs.size());
            size_t offsetInPinBuffer = 0;
            for (size_t blobIdx = 0; blobIdx < obj.blobs.size(); blobIdx++) {
                void* src = static_cast<uint8_t*>(deviceBuffer_[slot]) + offsetInPinBuffer;
                std::size_t taskId = 0;
                dispatcher_->MemcpyAsync(obj.blobs[blobIdx], src, obj.size, &taskId);
                size_t chainIdx = blobIdx % DMA_PARALLEL;
                if (lastTaskId[chainIdx] != std::size_t(-1)) {
                    dispatcher_->AddTaskDependency(lastTaskId[chainIdx], taskId);
                }
                lastTaskId[chainIdx] = taskId;
                offsetInPinBuffer += obj.size;
            }
            auto readyCount = std::min(obj.blobs.size(), DMA_PARALLEL);
            dispatcher_->LaunchDmaTask(dmaStream_, readyCount, 0);
            ASCEND_ASSERT(rtNotifyRecord(toDestDone_[slot], dmaStream_));
        }
        auto submitCost = submit.Elapse();
        ASCEND_ASSERT(aclrtSynchronizeStream(h2dStream_));
        ASCEND_ASSERT(aclrtSynchronizeStream(dmaStream_));
        auto transCost = trans.Elapse();
        return {transCost, submitCost};
    }
    void OnTransPost() override
    {
        ASCEND_ASSERT(aclrtSetDevice(device_));
        for (auto& obj : objects_) { ASCEND_ASSERT(aclrtFreeHost(obj.pinBuffer)); }
    }

public:
    TransH2DDmaTemplate(std::size_t device, std::size_t size, std::size_t iterations,
                        bool affinitySrc)
        : TransTemplate{iterations, affinitySrc}, device_{device}, size_{size}
    {
        const auto maxObjSize = size_ * MAX_NBLOB_PER_OBJ;
        dispatcher_ = std::make_unique<TransDmaDispatcher>(device);
        dispatcher_->CreateDmaCtxs(1);
        dispatcher_->SetDmaCtx(0);
        ASCEND_ASSERT(aclrtCreateStream(&h2dStream_));
        ASCEND_ASSERT(aclrtCreateStream(&dmaStream_));
        for (std::size_t i = 0; i < PIPE_SLOT_NUM; i++) {
            ASCEND_ASSERT(aclrtMalloc(&deviceBuffer_[i], maxObjSize, ACL_MEM_MALLOC_HUGE_FIRST));
            ASCEND_ASSERT(rtNotifyCreate(device, &toPinDone_[i]));
            ASCEND_ASSERT(rtNotifyCreate(device, &toDestDone_[i]));
        }
    }
    ~TransH2DDmaTemplate()
    {
        ASCEND_ASSERT(aclrtSetDevice(device_));
        ASCEND_ASSERT(aclrtDestroyStream(h2dStream_));
        ASCEND_ASSERT(aclrtDestroyStream(dmaStream_));
        for (std::size_t i = 0; i < PIPE_SLOT_NUM; i++) {
            ASCEND_ASSERT(aclrtFree(deviceBuffer_[i]));
            ASCEND_ASSERT(rtNotifyDestroy(toPinDone_[i]));
            ASCEND_ASSERT(rtNotifyDestroy(toDestDone_[i]));
        }
    }
    std::string Name() const override { return "dma"; }
};

#endif
