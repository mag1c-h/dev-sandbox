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
#include "trans_ffts_dispatcher_ascend.h"
#include <cstring>
#include "trans_assert_ascend.h"

TransFftsDispatcher::TransFftsDispatcher(std::size_t device)
{
    ASCEND_ASSERT(aclrtSetDevice(device));
}

TransFftsDispatcher::~TransFftsDispatcher() = default;

void TransFftsDispatcher::CreateFftsCtxs(std::size_t amount)
{
    ctxs_.resize(amount);
    for (std::size_t i = 0; i < amount; i++) {
        ctxs_[i].contexts.resize(128);
        ctxs_[i].refreshIndex = 0;
        ctxs_[i].ctxNum = 0;
    }
}

void TransFftsDispatcher::SetFftsCtx(std::size_t index) { currentCtx_ = &ctxs_[index]; }

void TransFftsDispatcher::MemcpyAsync(void* dst, const void* src, std::size_t size,
                                      std::size_t* taskId)
{
    if (currentCtx_->refreshIndex >= currentCtx_->contexts.size()) {
        currentCtx_->contexts.resize(currentCtx_->contexts.size() * 2);
    }
    auto& ctx = currentCtx_->contexts[currentCtx_->refreshIndex];
    std::memset(&ctx, 0, sizeof(ctx));
    rtFftsPlusSdmaCtx_t* sdmaCtx = reinterpret_cast<rtFftsPlusSdmaCtx_t*>(&ctx);
    sdmaCtx->contextType = RT_CTX_TYPE_SDMA;
    sdmaCtx->threadDim = 1;
    sdmaCtx->sdmaSqeHeader = 0x1E70;  // SDMA_FP32_ATOMIC_MOVE_SQE
    const uint64_t lowMask = 0x00000000ffffffffULL;
    const uint64_t highMask = 0xffffffff00000000ULL;
    sdmaCtx->sourceAddressBaseL = reinterpret_cast<uint64_t>(src) & lowMask;
    sdmaCtx->sourceAddressBaseH = (reinterpret_cast<uint64_t>(src) & highMask) >> 32;
    sdmaCtx->destinationAddressBaseL = reinterpret_cast<uint64_t>(dst) & lowMask;
    sdmaCtx->destinationAddressBaseH = (reinterpret_cast<uint64_t>(dst) & highMask) >> 32;
    sdmaCtx->nonTailDataLength = size;
    sdmaCtx->tailDataLength = size;
    *taskId = currentCtx_->refreshIndex;
    currentCtx_->refreshIndex++;
}

void TransFftsDispatcher::AddTaskDependency(std::size_t predecessorId, std::size_t successorId)
{
    auto& predCtx = currentCtx_->contexts[predecessorId];
    TRANS_ASSERT(predCtx.successorNum < RT_CTX_SUCCESSOR_NUM);
    predCtx.successorList[predCtx.successorNum] = successorId;
    predCtx.successorNum++;
    auto& succCtx = currentCtx_->contexts[successorId];
    succCtx.predCntInit++;
    succCtx.predCnt++;
}

void TransFftsDispatcher::LaunchFftsTask(aclrtStream stream, std::size_t readyCount,
                                         std::size_t ctxIndex)
{
    currentCtx_->ctxNum = currentCtx_->refreshIndex;
    if (currentCtx_->ctxNum == 0) { return; }
    rtFftsPlusSqe_t sqe = {};
    sqe.fftsType = RT_FFTS_PLUS_TYPE;
    sqe.totalContextNum = currentCtx_->ctxNum;
    sqe.readyContextNum = readyCount;
    sqe.preloadContextNum = (readyCount <= 128) ? readyCount : 128;
    sqe.timeout = 0;
    sqe.subType = 0x5A;  // Communication task

    rtFftsPlusTaskInfo_t task = {};
    task.fftsPlusSqe = &sqe;
    task.descBuf = currentCtx_->contexts.data();
    task.descBufLen = currentCtx_->ctxNum * sizeof(rtFftsPlusComCtx_t);
    task.descAddrType = 0;
    task.argsHandleInfoNum = 0;
    task.argsHandleInfoPtr = nullptr;

    uintptr_t input[2];
    input[0] = reinterpret_cast<uintptr_t>(&task);
    input[1] = reinterpret_cast<uintptr_t>(stream);

    auto ret = rtGeneralCtrl(input, 2, RT_GNL_CTRL_TYPE_FFTS_PLUS);
    TRANS_ASSERT(ret == 0);
}

void TransFftsDispatcher::ReuseCtx(std::size_t index)
{
    ctxs_[index].refreshIndex = 0;
    ctxs_[index].ctxNum = 0;
}
