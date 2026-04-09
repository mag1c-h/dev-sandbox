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
#include <chrono>
#include "copy_instance.h"
#include "error_handle.h"

struct CopyStreamContext {
    size_t size;
    std::vector<void*> src;
    std::vector<void*> dst;
};

static std::vector<CopyStreamContext> Prepare(const std::vector<const CopyBuffer*>& srcBuffers,
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

static std::pair<size_t, std::vector<size_t>> DoCopyImpl(
    const CopyInitiator* initiator, const std::vector<CopyStreamContext>& contexts)
{
    using namespace std::chrono;
    const auto number = contexts.size();
    auto tpStart = steady_clock::now().time_since_epoch();
    std::vector<size_t> submitCosts;
    for (size_t i = 0; i < number; i++) {
        auto& ctx = contexts[i];
        auto tp = steady_clock::now().time_since_epoch();
        initiator->Copy(ctx.src.data(), ctx.dst.data(), ctx.size, ctx.src.size(), nullptr);
        auto submitCost = steady_clock::now().time_since_epoch() - tp;
        submitCosts.push_back(duration_cast<microseconds>(submitCost).count());
    }
    auto copyCost = steady_clock::now().time_since_epoch() - tpStart;
    return {duration_cast<microseconds>(copyCost).count(), submitCosts};
}

static void CleanUp(const std::vector<CopyStreamContext>& contexts) {}

CopyResult::Result CopyInstance::DoCopy(const std::vector<const CopyBuffer*>& srcBuffers,
                                        const std::vector<const CopyBuffer*>& dstBuffers) const
{
    auto contexts = Prepare(srcBuffers, dstBuffers);
    constexpr auto warmup = 3;
    for (auto i = 0; i < warmup; i++) { DoCopyImpl(initiator_, contexts); }
    std::vector<size_t> submitCostArray;
    std::vector<size_t> copyCostArray;
    for (size_t i = 0; i < iterations_; i++) {
        auto [copyCost, submitCost] = DoCopyImpl(initiator_, contexts);
        copyCostArray.push_back(copyCost);
        submitCostArray.insert(submitCostArray.end(), std::make_move_iterator(submitCost.begin()),
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
