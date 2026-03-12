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
#ifndef MEMBW_MEMORY_BUFFER_H
#define MEMBW_MEMORY_BUFFER_H

#include <fcntl.h>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include "error_handle.h"

class MemoryBuffer {
protected:
    void* buffer_;
    size_t size_;
    size_t number_;

public:
    MemoryBuffer(size_t size, size_t number) : buffer_(nullptr), size_(size), number_(number) {}
    virtual ~MemoryBuffer() = default;
    virtual std::string ReadMe() const = 0;
    void* Buffer() const { return buffer_; }
    void* operator[](size_t index) const
    {
        MEMBW_ASSERT(index < number_);
        return static_cast<char*>(buffer_) + index * size_;
    }
    size_t Size() const { return size_; }
    size_t Number() const { return number_; }
};

class MmapAnonymousBuffer : public MemoryBuffer {
public:
    MmapAnonymousBuffer(size_t size, size_t number) : MemoryBuffer(size, number)
    {
        auto totalSize = size_ * number_;
        buffer_ = mmap(nullptr, totalSize, PROT_READ | PROT_WRITE,
                       MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE, -1, 0);
        MEMBW_ASSERT(buffer_ != MAP_FAILED);
    }
    ~MmapAnonymousBuffer() override
    {
        if (buffer_) { munmap(buffer_, size_ * number_); }
    }
    std::string ReadMe() const override { return "MmapAnonymousBuffer"; }
};

class MmapSharedBuffer : public MemoryBuffer {
public:
    MmapSharedBuffer(size_t size, size_t number) : MemoryBuffer(size, number)
    {
        auto totalSize = size_ * number_;
        auto fd = shm_open("membw_shared_buffer", O_CREAT | O_RDWR, 0666);
        MEMBW_ASSERT(fd != -1);
        MEMBW_ASSERT(ftruncate(fd, totalSize) != -1);
        buffer_ =
            mmap(nullptr, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, 0);
        MEMBW_ASSERT(buffer_ != MAP_FAILED);
        close(fd);
        shm_unlink("membw_shared_buffer");
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
    PosixMemalignBuffer(size_t size, size_t number, size_t alignment)
        : MemoryBuffer(size, number), alignment_(alignment)
    {
        MEMBW_ASSERT(posix_memalign(&buffer_, alignment_, size_ * number_) == 0);
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

#endif  // MEMBW_MEMORY_BUFFER_H
