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
#include <cstring>
#include "copy_buffer.h"
#include "error_handle_cuda.h"

CopyBufferHost::CopyBufferHost(size_t device, size_t size, size_t number)
    : CopyBuffer{device, size, number}
{
    const auto total = size * number;
    CUDA_ASSERT(cudaMallocHost(&addr_, total));
    std::memset(addr_, 'h', total);
}

CopyBufferHost::~CopyBufferHost()
{
    if (addr_) { CUDA_ASSERT(cudaFreeHost(addr_)); }
}

std::string CopyBufferHost::Name() const { return "cuda::host"; }

CopyBufferDevice::CopyBufferDevice(size_t device, size_t size, size_t number)
    : CopyBuffer{device, size, number}
{
    const auto total = size * number;
    CUDA_ASSERT(cudaSetDevice(device_));
    CUDA_ASSERT(cudaMalloc(&addr_, total));
    CUDA_ASSERT(cudaMemset(addr_, 'd', total));
}

CopyBufferDevice::~CopyBufferDevice()
{
    if (addr_) {
        CUDA_ASSERT(cudaSetDevice(device_));
        CUDA_ASSERT(cudaFree(addr_));
    }
}

std::string CopyBufferDevice::Name() const { return "cuda::device::" + std::to_string(device_); }
