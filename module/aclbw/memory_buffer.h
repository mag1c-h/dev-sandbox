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
#ifndef ACLBW_MEMORY_BUFFER_H
#define ACLBW_MEMORY_BUFFER_H

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
        ACLBW_ASSERT(index < number_);
        return static_cast<char*>(buffer_) + index * size_;
    }
    int32_t DeviceId() const { return deviceId_; }
    size_t Size() const { return size_; }
    size_t Number() const { return number_; }
};

class MmapSharedRegisteredBuffer : public MemoryBuffer {
public:
    MmapSharedRegisteredBuffer(int32_t deviceId, size_t size, size_t number)
        : MemoryBuffer(deviceId, size, number)
    {
        ACLBW_ASCEND_ASSERT(aclrtSetDevice(deviceId_));
        auto totalSize = size_ * number_;
        auto fd = shm_open("aclbw_shared_buffer", O_CREAT | O_RDWR, 0666);
        ACLBW_ASSERT(fd != -1);
        ACLBW_ASSERT(ftruncate(fd, totalSize) != -1);
        buffer_ =
            mmap(nullptr, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, 0);
        ACLBW_ASSERT(buffer_ != MAP_FAILED);
        close(fd);
        shm_unlink("aclbw_shared_buffer");
        ACLBW_ASCEND_ASSERT(
            aclrtHostRegisterV2(buffer_, totalSize, ACL_HOST_REG_MAPPED | ACL_HOST_REG_PINNED));
    }
    MmapSharedRegisteredBuffer(const char* shmName, int32_t deviceId, size_t size, size_t number)
        : MemoryBuffer(deviceId, size, number)
    {
        ACLBW_ASCEND_ASSERT(aclrtSetDevice(deviceId_));
        auto totalSize = size_ * number_;
        auto fd = shm_open(shmName, O_CREAT | O_RDWR, 0666);
        ACLBW_ASSERT(fd != -1);
        ACLBW_ASSERT(ftruncate(fd, totalSize) != -1);
        buffer_ =
            mmap(nullptr, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, 0);
        ACLBW_ASSERT(buffer_ != MAP_FAILED);
        close(fd);
        ACLBW_ASCEND_ASSERT(
            aclrtHostRegisterV2(buffer_, totalSize, ACL_HOST_REG_MAPPED | ACL_HOST_REG_PINNED));
    }
    ~MmapSharedRegisteredBuffer() override
    {
        if (buffer_) {
            ACLBW_ASCEND_ASSERT(aclrtSetDevice(deviceId_));
            ACLBW_ASCEND_ASSERT(aclrtHostUnregister(buffer_));
            munmap(buffer_, size_ * number_);
        }
    }
    std::string ReadMe() const override { return "MmapSharedRegisteredBuffer"; }
};

class AscendHostMemoryBuffer : public MemoryBuffer {
public:
    AscendHostMemoryBuffer(int32_t deviceId, size_t size, size_t number)
        : MemoryBuffer(deviceId, size, number)
    {
        ACLBW_ASCEND_ASSERT(aclrtSetDevice(deviceId_));
        ACLBW_ASCEND_ASSERT(aclrtMallocHost(&buffer_, size_ * number_));
    }
    ~AscendHostMemoryBuffer() override
    {
        if (buffer_) {
            ACLBW_ASCEND_ASSERT(aclrtSetDevice(deviceId_));
            ACLBW_ASCEND_ASSERT(aclrtFreeHost(buffer_));
        }
    }
    std::string ReadMe() const override { return "AscendHostMemoryBuffer"; }
};

class AscendDeviceMemoryBuffer : public MemoryBuffer {
public:
    AscendDeviceMemoryBuffer(int32_t deviceId, size_t size, size_t number)
        : MemoryBuffer(deviceId, size, number)
    {
        ACLBW_ASCEND_ASSERT(aclrtSetDevice(deviceId_));
        ACLBW_ASCEND_ASSERT(aclrtMalloc(&buffer_, size_ * number_, ACL_MEM_MALLOC_HUGE_FIRST));
    }
    ~AscendDeviceMemoryBuffer() override
    {
        if (buffer_) {
            ACLBW_ASCEND_ASSERT(aclrtSetDevice(deviceId_));
            ACLBW_ASCEND_ASSERT(aclrtFree(buffer_));
        }
    }
    std::string ReadMe() const override { return "AscendDeviceMemoryBuffer"; }
};

#endif
