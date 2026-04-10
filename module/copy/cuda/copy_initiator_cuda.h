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
#ifndef COPY_INITIATOR_CUDA_H
#define COPY_INITIATOR_CUDA_H

#include "copy_initiator.h"
#include "error_handle_cuda.h"

extern cudaError_t CudaSMCopyBatchAsync(void* src[], void* dst[], size_t size, size_t number,
                                        cudaStream_t stream);

class H2DCopyInitiator : public CopyInitiator {
public:
    std::string Name() const override { return "CE"; }
    void Copy(void* const* src, void* const* dst, size_t size, size_t number,
              void* args) const override
    {
        auto stream = static_cast<cudaStream_t>(args);
        for (size_t i = 0; i < number; ++i) {
            CUDA_ASSERT(cudaMemcpyAsync(dst[i], src[i], size, cudaMemcpyHostToDevice, stream));
        }
    }
};

class D2DCopyInitiator : public CopyInitiator {
public:
    std::string Name() const override { return "CE"; }
    void Copy(void* const* src, void* const* dst, size_t size, size_t number,
              void* args) const override
    {
        auto stream = static_cast<cudaStream_t>(args);
        for (size_t i = 0; i < number; ++i) {
            CUDA_ASSERT(cudaMemcpyAsync(dst[i], src[i], size, cudaMemcpyDeviceToDevice, stream));
        }
    }
};

class SMCopyInitiator : public CopyInitiator {
    void* dSrc_{nullptr};
    void* dDst_{nullptr};

public:
    SMCopyInitiator(size_t device, size_t number) : CopyInitiator{}
    {
        const auto ptrSize = sizeof(void*) * number;
        CUDA_ASSERT(cudaSetDevice(device));
        CUDA_ASSERT(cudaMalloc(&dSrc_, ptrSize));
        CUDA_ASSERT(cudaMalloc(&dDst_, ptrSize));
    }
    ~SMCopyInitiator() override
    {
        if (dSrc_) { CUDA_ASSERT(cudaFree(dSrc_)); }
        if (dDst_) { CUDA_ASSERT(cudaFree(dDst_)); }
    }
    std::string Name() const override { return "SM"; }
    void Copy(void* const* src, void* const* dst, size_t size, size_t number,
              void* args) const override
    {
        auto stream = static_cast<cudaStream_t>(args);
        const auto ptrSize = sizeof(void*) * number;
        CUDA_ASSERT(cudaMemcpyAsync(dSrc_, src, ptrSize, cudaMemcpyHostToDevice, stream));
        CUDA_ASSERT(cudaMemcpyAsync(dDst_, dst, ptrSize, cudaMemcpyHostToDevice, stream));
        CUDA_ASSERT(CudaSMCopyBatchAsync(static_cast<void**>(dSrc_), static_cast<void**>(dDst_),
                                         size, number, stream));
    }
};

#endif  // COPY_INITIATOR_CUDA_H
