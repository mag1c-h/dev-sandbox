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
#ifndef COPY_INSTANCE_ASCEND_H
#define COPY_INSTANCE_ASCEND_H

#include <chrono>
#include "copy_instance.h"
#include "error_handle_ascend.h"

class AscendCopyInstance : public CopyInstance {
    struct CopyStreamContext {
        size_t deviceId;
        aclrtStream stream;
        aclrtEvent endEvent;
        size_t size;
        std::vector<void*> src;
        std::vector<void*> dst;
    };

    size_t AffinityDeviceId(const CopyBuffer& src, const CopyBuffer& dst) const
    {
        return (affinitySrc_ ? src : dst).Device();
    }
    std::vector<CopyStreamContext> Prepare(const std::vector<const CopyBuffer*>& srcBuffers,
                                           const std::vector<const CopyBuffer*>& dstBuffers) const
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
            ASCEND_ASSERT(aclrtSetDevice(ctx.deviceId));
            ASCEND_ASSERT(aclrtCreateStream(&ctx.stream));
            ASCEND_ASSERT(aclrtCreateEvent(&ctx.endEvent));
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
    std::pair<size_t, std::vector<size_t>> DoCopyImpl(
        const std::vector<CopyStreamContext>& contexts) const
    {
        using namespace std::chrono;
        const auto number = contexts.size();
        aclrtEvent totalStart, totalEnd;
        auto& firstCtx = contexts[0];
        ASCEND_ASSERT(aclrtSetDevice(firstCtx.deviceId));
        ASCEND_ASSERT(aclrtCreateEvent(&totalStart));
        ASCEND_ASSERT(aclrtCreateEvent(&totalEnd));
        ASCEND_ASSERT(aclrtRecordEvent(totalStart, firstCtx.stream));
        std::vector<size_t> submitCosts;
        for (size_t i = 0; i < number; i++) {
            auto& ctx = contexts[i];
            ASCEND_ASSERT(aclrtSetDevice(ctx.deviceId));
            if (i != 0) { ASCEND_ASSERT(aclrtStreamWaitEvent(ctx.stream, totalStart)); }
            auto tp = steady_clock::now().time_since_epoch();
            initiator_->Copy(ctx.src.data(), ctx.dst.data(), ctx.size, ctx.src.size(), ctx.stream);
            auto submitCost = steady_clock::now().time_since_epoch() - tp;
            submitCosts.push_back(duration_cast<microseconds>(submitCost).count());
            if (i != 0) {
                ASCEND_ASSERT(aclrtRecordEvent(ctx.endEvent, ctx.stream));
                ASCEND_ASSERT(aclrtSetDevice(firstCtx.deviceId));
                ASCEND_ASSERT(aclrtStreamWaitEvent(firstCtx.stream, ctx.endEvent));
            }
        }
        ASCEND_ASSERT(aclrtSetDevice(firstCtx.deviceId));
        ASCEND_ASSERT(aclrtRecordEvent(totalEnd, firstCtx.stream));
        ASCEND_ASSERT(aclrtSynchronizeEvent(totalEnd));
        float copyCostMs = 0.f;
        ASCEND_ASSERT(aclrtEventElapsedTime(&copyCostMs, totalStart, totalEnd));
        ASCEND_ASSERT(aclrtDestroyEvent(totalStart));
        ASCEND_ASSERT(aclrtDestroyEvent(totalEnd));
        return {copyCostMs * 1e3, submitCosts};
    }
    void CleanUp(const std::vector<CopyStreamContext>& contexts) const
    {
        for (auto& ctx : contexts) {
            ASCEND_ASSERT(aclrtSetDevice(ctx.deviceId));
            ASCEND_ASSERT(aclrtDestroyEvent(ctx.endEvent));
            ASCEND_ASSERT(aclrtDestroyStream(ctx.stream));
        }
    }

public:
    using CopyInstance::CopyInstance;
    CopyResult::Result DoCopyBatch(const std::vector<const CopyBuffer*>& srcBuffers,
                                   const std::vector<const CopyBuffer*>& dstBuffers) const override
    {
        auto contexts = Prepare(srcBuffers, dstBuffers);
        constexpr auto warmup = 3;
        for (auto i = 0; i < warmup; i++) { DoCopyImpl(contexts); }
        std::vector<size_t> submitCostArray;
        std::vector<size_t> copyCostArray;
        for (size_t i = 0; i < iterations_; i++) {
            auto [copyCost, submitCost] = DoCopyImpl(contexts);
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
};

#endif  // COPY_INSTANCE_ASCEND_H
