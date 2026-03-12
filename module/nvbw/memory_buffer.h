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
#ifndef NVBW_MEMORY_BUFFER_H
#define NVBW_MEMORY_BUFFER_H

#include <fcntl.h>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include "error_handle.h"

class MemoryBuffer {
protected:
    void* buffer_;
    int32_t deviceId_;
    size_t size_;
    size_t number_;

public:
    MemoryBuffer(int32_t deviceId, size_t size, size_t number)
        : buffer_(nullptr), deviceId_(deviceId), size_(size), number_(number)
    {
    }
    virtual ~MemoryBuffer() = default;
    virtual std::string ReadMe() const = 0;
    void* Buffer() const { return buffer_; }
    void* operator[](size_t index) const
    {
        NVBW_ASSERT(index < number_);
        return static_cast<char*>(buffer_) + index * size_;
    }
    int32_t DeviceId() const { return deviceId_; }
    size_t Size() const { return size_; }
    size_t Number() const { return number_; }
};

class MmapSharedBuffer : public MemoryBuffer {
public:
    MmapSharedBuffer(int32_t deviceId, size_t size, size_t number)
        : MemoryBuffer(deviceId, size, number)
    {
        auto totalSize = size_ * number_;
        auto fd = shm_open("nvbw_shared_buffer", O_CREAT | O_RDWR, 0666);
        NVBW_ASSERT(fd != -1);
        NVBW_ASSERT(ftruncate(fd, totalSize) != -1);
        buffer_ =
            mmap(nullptr, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, 0);
        NVBW_ASSERT(buffer_ != MAP_FAILED);
        close(fd);
        shm_unlink("nvbw_shared_buffer");
    }
    ~MmapSharedBuffer() override
    {
        if (buffer_) { munmap(buffer_, size_ * number_); }
    }
    std::string ReadMe() const override { return "MmapSharedBuffer"; }
};

class PosixMemalignBuffer : public MemoryBuffer {
    size_t alignment_;

public:
    PosixMemalignBuffer(int32_t deviceId, size_t size, size_t number, size_t alignment)
        : MemoryBuffer(deviceId, size, number), alignment_(alignment)
    {
        NVBW_ASSERT(posix_memalign(&buffer_, alignment_, size_ * number_) == 0);
    }
    ~PosixMemalignBuffer() override
    {
        if (buffer_) { free(buffer_); }
    }
    std::string ReadMe() const override
    {
        return "PosixMemalignBuffer" + std::to_string(alignment_);
    }
};

class MmapSharedRegisteredBuffer : public MemoryBuffer {
public:
    MmapSharedRegisteredBuffer(int32_t deviceId, size_t size, size_t number)
        : MemoryBuffer(deviceId, size, number)
    {
        NVBW_CUDA_ASSERT(cudaSetDevice(deviceId_));
        auto totalSize = size_ * number_;
        auto fd = shm_open("nvbw_shared_buffer", O_CREAT | O_RDWR, 0666);
        NVBW_ASSERT(fd != -1);
        NVBW_ASSERT(ftruncate(fd, totalSize) != -1);
        buffer_ =
            mmap(nullptr, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, 0);
        NVBW_ASSERT(buffer_ != MAP_FAILED);
        close(fd);
        shm_unlink("nvbw_shared_buffer");
        NVBW_CUDA_ASSERT(cudaHostRegister(buffer_, totalSize, cudaHostRegisterDefault));
    }
    MmapSharedRegisteredBuffer(const char* shmName, int32_t deviceId, size_t size, size_t number)
        : MemoryBuffer(deviceId, size, number)
    {
        NVBW_CUDA_ASSERT(cudaSetDevice(deviceId_));
        auto totalSize = size_ * number_;
        auto fd = shm_open(shmName, O_CREAT | O_RDWR, 0666);
        NVBW_ASSERT(fd != -1);
        NVBW_ASSERT(ftruncate(fd, totalSize) != -1);
        buffer_ =
            mmap(nullptr, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, 0);
        NVBW_ASSERT(buffer_ != MAP_FAILED);
        close(fd);
        NVBW_CUDA_ASSERT(cudaHostRegister(buffer_, totalSize, cudaHostRegisterDefault));
    }
    ~MmapSharedRegisteredBuffer() override
    {
        if (buffer_) {
            NVBW_CUDA_ASSERT(cudaSetDevice(deviceId_));
            NVBW_CUDA_ASSERT(cudaHostUnregister(buffer_));
            munmap(buffer_, size_ * number_);
        }
    }
    std::string ReadMe() const override { return "MmapSharedRegisteredBuffer"; }
};

class PosixMemalignRegisteredBuffer : public MemoryBuffer {
    size_t alignment_;

public:
    PosixMemalignRegisteredBuffer(int32_t deviceId, size_t size, size_t number, size_t alignment)
        : MemoryBuffer(deviceId, size, number), alignment_(alignment)
    {
        NVBW_CUDA_ASSERT(cudaSetDevice(deviceId_));
        auto totalSize = size_ * number_;
        NVBW_ASSERT(posix_memalign(&buffer_, alignment_, size_ * number_) == 0);
        NVBW_CUDA_ASSERT(cudaHostRegister(buffer_, totalSize, cudaHostRegisterDefault));
    }
    ~PosixMemalignRegisteredBuffer() override
    {
        if (buffer_) {
            NVBW_CUDA_ASSERT(cudaSetDevice(deviceId_));
            NVBW_CUDA_ASSERT(cudaHostUnregister(buffer_));
            free(buffer_);
        }
    }
    std::string ReadMe() const override
    {
        return "PosixMemalignRegisteredBuffer" + std::to_string(alignment_);
    }
};

class CudaHostMemoryBuffer : public MemoryBuffer {
public:
    CudaHostMemoryBuffer(int32_t deviceId, size_t size, size_t number)
        : MemoryBuffer(deviceId, size, number)
    {
        NVBW_CUDA_ASSERT(cudaSetDevice(deviceId_));
        NVBW_CUDA_ASSERT(cudaMallocHost(&buffer_, size_ * number_));
    }
    ~CudaHostMemoryBuffer() override
    {
        if (buffer_) {
            NVBW_CUDA_ASSERT(cudaSetDevice(deviceId_));
            NVBW_CUDA_ASSERT(cudaFreeHost(buffer_));
        }
    }
    std::string ReadMe() const override { return "CudaHostMemoryBuffer"; }
};

class CudaDeviceMemoryBuffer : public MemoryBuffer {
public:
    CudaDeviceMemoryBuffer(int32_t deviceId, size_t size, size_t number)
        : MemoryBuffer(deviceId, size, number)
    {
        NVBW_CUDA_ASSERT(cudaSetDevice(deviceId_));
        NVBW_CUDA_ASSERT(cudaMalloc(&buffer_, size_ * number_));
    }
    ~CudaDeviceMemoryBuffer() override
    {
        if (buffer_) {
            NVBW_CUDA_ASSERT(cudaSetDevice(deviceId_));
            NVBW_CUDA_ASSERT(cudaFree(buffer_));
        }
    }
    std::string ReadMe() const override { return "CudaDeviceMemoryBuffer"; }
};

#endif
