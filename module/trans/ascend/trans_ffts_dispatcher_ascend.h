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
#ifndef TRANS_FFTS_DISPATCHER_ASCEND_H
#define TRANS_FFTS_DISPATCHER_ASCEND_H

#include <acl/acl.h>
#include <runtime/rt.h>
#include <vector>

class TransFftsDispatcher {
    struct FftsCtx {
        std::vector<rtFftsPlusComCtx_t> contexts;
        uint32_t refreshIndex = 0;
        uint32_t ctxNum = 0;
    };

    std::vector<FftsCtx> ctxs_;
    FftsCtx* currentCtx_ = nullptr;

public:
    TransFftsDispatcher(std::size_t device);
    ~TransFftsDispatcher();
    void CreateFftsCtxs(std::size_t amount);
    void SetFftsCtx(std::size_t index);
    void MemcpyAsync(void* dst, const void* src, std::size_t size, std::size_t* taskId);
    void AddTaskDependency(std::size_t predecessorId, std::size_t successorId);
    void LaunchFftsTask(aclrtStream stream, std::size_t readyCount, std::size_t ctxIndex);
    void ReuseCtx(std::size_t index);
};

#endif  // TRANS_FFTS_DISPATCHER_ASCEND_H
