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

#pragma once

#include "address.h"
#include "error.h"

namespace ucm::transfer {

struct IoTask {
    uint64_t src;
    uint64_t dst;
    std::size_t size;
};

struct TaskResult {
    uint64_t task_id = 0;
    std::size_t bytes_transferred = 0;
    Error error;

    bool ok() const { return error.ok(); }
};

struct SyncResult {
    std::size_t total_tasks = 0;
    std::size_t succeeded = 0;
    std::size_t failed = 0;
    std::size_t bytes_total = 0;
    Error first_error;
    uint64_t first_failed_task_id = 0;

    bool ok() const { return first_error.ok(); }
};

class IStream {
public:
    virtual ~IStream() = default;
    virtual Expected<uint64_t> submit(IoTask task) = 0;
    virtual TaskResult synchronize(uint64_t task_id) = 0;
    virtual SyncResult synchronize() = 0;
    virtual void cancel() = 0;
    virtual void close() = 0;
    virtual std::size_t pending_count() const = 0;
    virtual bool is_open() const = 0;
    virtual const AnyAddress& source() const = 0;
    virtual const AnyAddress& destination() const = 0;
};

}  // namespace ucm::transfer
