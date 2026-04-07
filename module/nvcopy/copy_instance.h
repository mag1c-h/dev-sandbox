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
#ifndef COPY_INSTANCE_H
#define COPY_INSTANCE_H

#include <chrono>
#include "copy_buffer.h"
#include "copy_initiator.h"
#include "copy_result.h"

class CopyInstance {
    CopyInitiator* initiator_;
    size_t iterations_;
    bool affinitySrc_;

    struct CopyStreamContext {
        size_t deviceId;
        cudaStream_t stream;
        cudaEvent_t endEvent;
        size_t size;
        std::vector<void*> src;
        std::vector<void*> dst;
    };

    size_t AffinityDeviceId(const CopyBuffer& src, const CopyBuffer& dst)
    {
        return (affinitySrc_ ? src : dst).Device();
    }
    std::vector<CopyStreamContext> Prepare(const std::vector<const CopyBuffer*>& srcBuffers,
                                           const std::vector<const CopyBuffer*>& dstBuffers)
    {
        const auto bufferNumber = srcBuffers.size();
        std::vector<CopyStreamContext> contexts(bufferNumber);
        for (size_t i = 0; i < bufferNumber; i++) {
            auto& ctx = contexts[i];
            auto& src = *srcBuffers[i];
            auto& dst = *dstBuffers[i];
            ASSERT(src.Number() == dst.Number());
            ASSERT(src.Size() == dst.Size());
            ctx.deviceId = AffinityDeviceId(src, dst);
            CUDA_ASSERT(cudaSetDevice(ctx.deviceId));
            CUDA_ASSERT(cudaStreamCreateWithFlags(&ctx.stream, cudaStreamNonBlocking));
            CUDA_ASSERT(cudaEventCreate(&ctx.endEvent, cudaEventDefault));
            ctx.size = src.Size();
            ctx.src.reserve(src.Number());
            ctx.dst.reserve(dst.Number());
            for (size_t j = 0; j < src.Number(); j++) {
                ctx.src.push_back(src[j]);
                ctx.dst.push_back(dst[j]);
            }
        }
        return contexts;
    }
    std::pair<size_t, std::vector<size_t>> DoCopy(const std::vector<CopyStreamContext>& contexts)
    {
        using namespace std::chrono;
        const auto number = contexts.size();
        cudaEvent_t totalStart, totalEnd;
        auto& firstCtx = contexts[0];
        CUDA_ASSERT(cudaSetDevice(firstCtx.deviceId));
        CUDA_ASSERT(cudaEventCreate(&totalStart));
        CUDA_ASSERT(cudaEventCreate(&totalEnd));
        CUDA_ASSERT(cudaEventRecord(totalStart, firstCtx.stream));
        std::vector<size_t> submitCosts;
        for (size_t i = 0; i < number; i++) {
            auto& ctx = contexts[i];
            CUDA_ASSERT(cudaSetDevice(ctx.deviceId));
            if (i != 0) { CUDA_ASSERT(cudaStreamWaitEvent(ctx.stream, totalStart)); }
            auto tp = steady_clock::now().time_since_epoch();
            initiator_->Copy(ctx.src.data(), ctx.dst.data(), ctx.size, ctx.src.size(), ctx.stream);
            auto submitCost = steady_clock::now().time_since_epoch() - tp;
            submitCosts.push_back(duration_cast<microseconds>(submitCost).count());
            if (i != 0) {
                CUDA_ASSERT(cudaEventRecord(ctx.endEvent, ctx.stream));
                CUDA_ASSERT(cudaSetDevice(firstCtx.deviceId));
                CUDA_ASSERT(cudaStreamWaitEvent(firstCtx.stream, ctx.endEvent));
            }
        }
        CUDA_ASSERT(cudaSetDevice(firstCtx.deviceId));
        CUDA_ASSERT(cudaEventRecord(totalEnd, firstCtx.stream));
        CUDA_ASSERT(cudaEventSynchronize(totalEnd));
        float copyCostMs = 0.f;
        CUDA_ASSERT(cudaEventElapsedTime(&copyCostMs, totalStart, totalEnd));
        CUDA_ASSERT(cudaEventDestroy(totalStart));
        CUDA_ASSERT(cudaEventDestroy(totalEnd));
        return {copyCostMs * 1e3, submitCosts};
    }
    void CleanUp(const std::vector<CopyStreamContext>& contexts)
    {
        for (auto& ctx : contexts) {
            CUDA_ASSERT(cudaSetDevice(ctx.deviceId));
            CUDA_ASSERT(cudaEventDestroy(ctx.endEvent));
            CUDA_ASSERT(cudaStreamDestroy(ctx.stream));
        }
    }

public:
    CopyInstance(CopyInitiator* initiator, size_t iterations, bool affinitySrc)
        : initiator_(initiator), iterations_(iterations), affinitySrc_(affinitySrc)
    {
    }
    CopyResult::Result DoCopy(const std::vector<const CopyBuffer*>& srcBuffers,
                              const std::vector<const CopyBuffer*>& dstBuffers)
    {
        auto contexts = Prepare(srcBuffers, dstBuffers);
        constexpr auto warmup = 3;
        for (auto i = 0; i < warmup; i++) { DoCopy(contexts); }
        std::vector<size_t> submitCostArray;
        std::vector<size_t> copyCostArray;
        for (size_t i = 0; i < iterations_; i++) {
            auto [copyCost, submitCost] = DoCopy(contexts);
            copyCostArray.push_back(copyCost);
            submitCostArray.insert(submitCostArray.end(),
                                   std::make_move_iterator(submitCost.begin()),
                                   std::make_move_iterator(submitCost.end()));
        }
        CleanUp(contexts);
        return {srcBuffers.front()->Name(),
                dstBuffers.front()->Name(),
                initiator_->Name(),
                srcBuffers.front()->Size(),
                srcBuffers.front()->Number() * srcBuffers.size(),
                std::move(submitCostArray),
                std::move(copyCostArray)};
    }
    CopyResult::Result DoCopy(const CopyBuffer* srcBuffer, const CopyBuffer* dstBuffer)
    {
        std::vector<const CopyBuffer*> srcBuffers{srcBuffer};
        std::vector<const CopyBuffer*> dstBuffers{dstBuffer};
        return DoCopy(srcBuffers, dstBuffers);
    }
};

#endif  // COPY_INSTANCE_H
