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
#ifndef COPY_INITIATOR_H
#define COPY_INITIATOR_H

#include "error_handle.h"

class CopyInitiator {
public:
    virtual ~CopyInitiator() = default;
    virtual std::string Name() const = 0;
    virtual void Copy(void* src, void* dst, size_t size, cudaStream_t stream) { ASSERT(false); }
    virtual void Copy(void* const* src, void* const* dst, size_t size, size_t number,
                      cudaStream_t stream)
    {
        for (size_t i = 0; i < number; i++) { Copy(src[i], dst[i], size, stream); }
    }
};

class CudaMemcpyHost2DeviceCopyInitiator : public CopyInitiator {
public:
    std::string Name() const override { return "CE"; }
    void Copy(void* src, void* dst, size_t size, cudaStream_t stream) override
    {
        CUDA_ASSERT(cudaMemcpyAsync(dst, src, size, cudaMemcpyHostToDevice, stream));
    }
};

class CudaMemcpyDevice2DeviceCopyInitiator : public CopyInitiator {
public:
    std::string Name() const override { return "CE"; }
    void Copy(void* src, void* dst, size_t size, cudaStream_t stream) override
    {
        CUDA_ASSERT(cudaMemcpyAsync(dst, src, size, cudaMemcpyDeviceToDevice, stream));
    }
};

#endif  // COPY_INITIATOR_H
