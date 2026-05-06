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
#include <atomic>
#include <fstream>
#include <map>
#include <mutex>
#include <utility>
#include <vector>
#include "address/local_file.h"
#include "protocol/sendfile.h"
#include "transfer/abstract/registry.h"
#include "transfer/abstract/stream.h"

namespace ucm::transfer {

class LocalFile2LocalFileSendfileStream : public IStream {
public:
    LocalFile2LocalFileSendfileStream(FileAddress src, FileAddress dst, SendfileProtocol protocol)
        : src_(std::move(src)),
          dst_(std::move(dst)),
          protocol_(protocol),
          src_wrapped_(src_),
          dst_wrapped_(dst_),
          open_(true),
          next_task_id_(1)
    {
        src_file_.open(src_.path, std::ios::binary | std::ios::in);
        dst_file_.open(dst_.path, std::ios::binary | std::ios::out);
        if (!src_file_.is_open() || !dst_file_.is_open()) { open_ = false; }
    }
    ~LocalFile2LocalFileSendfileStream() override { close(); }
    Expected<uint64_t> submit(IoTask task) override
    {
        if (!open_) { return Error{ErrorCode::StreamClosed, "Stream is closed"}; }

        uint64_t id = next_task_id_++;
        task_internal ti;
        ti.task = task;
        ti.id = id;
        ti.completed = false;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_[id] = ti;
        }

        return id;
    }

    Expected<std::size_t> synchronize(uint64_t task_id) override
    {
        if (!open_) { return Error{ErrorCode::StreamClosed, "Stream is closed"}; }

        task_internal ti;
        {
            std::lock_guard<std::mutex> lock(mutex_);

            auto it = tasks_.find(task_id);
            if (it == tasks_.end()) {
                return Error{ErrorCode::TaskNotFound, "Task not found: " + std::to_string(task_id)};
            }

            ti = std::move(it->second);
            tasks_.erase(it);
        }

        execute_task(ti);

        return ti.bytes_transferred;
    }

    SyncResult synchronize() override
    {
        if (!open_) {
            return SyncResult{
                .first_error = Error{ErrorCode::StreamClosed, "Stream is closed"}
            };
        }

        std::vector<std::pair<uint64_t, task_internal>> tasks_to_execute;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_to_execute.reserve(tasks_.size());
            for (auto& [id, ti] : tasks_) { tasks_to_execute.emplace_back(id, std::move(ti)); }
            tasks_.clear();
        }

        SyncResult result;
        result.total_tasks = tasks_to_execute.size();

        for (auto& [id, ti] : tasks_to_execute) {
            execute_task(ti);

            result.bytes_total += ti.bytes_transferred;

            if (ti.error.ok()) {
                result.succeeded++;
            } else {
                result.failed++;
                if (result.first_error.ok()) {
                    result.first_error = ti.error;
                    result.first_failed_task_id = id;
                }
            }
        }

        return result;
    }

    void cancel() override
    {
        std::lock_guard<std::mutex> lock(mutex_);

        for (auto& [id, ti] : tasks_) {
            ti.error = Error{ErrorCode::Cancelled, "Task cancelled"};
            ti.completed = true;
        }
    }

    void close() override
    {
        if (open_) {
            open_ = false;
            src_file_.close();
            dst_file_.close();

            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.clear();
        }
    }

    size_t pending_count() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.size();
    }

    bool is_open() const override { return open_; }

    const AnyAddress& source() const override { return src_wrapped_; }

    const AnyAddress& destination() const override { return dst_wrapped_; }

private:
    struct task_internal {
        IoTask task;
        uint64_t id;
        bool completed;
        size_t bytes_transferred = 0;
        Error error;
    };

    void execute_task(task_internal& ti)
    {
        if (!src_file_.is_open() || !dst_file_.is_open()) {
            ti.error = Error{ErrorCode::SourceNotFound, "File not open"};
            ti.completed = true;
            return;
        }

        uint64_t src_offset = ti.task.src;
        uint64_t dst_offset = ti.task.dst;

        size_t size = ti.task.size;
        if (size == 0) {
            ti.error = Error{ErrorCode::InvalidTask, "Task size is 0"};
            ti.completed = true;
            return;
        }

        std::vector<char> buffer(protocol_.chunk_size);

        src_file_.seekg(src_offset);
        dst_file_.seekp(dst_offset);

        size_t remaining = size;
        size_t total_transferred = 0;

        while (remaining > 0 && !src_file_.eof() && src_file_.good() && dst_file_.good()) {
            size_t to_read = std::min(remaining, protocol_.chunk_size);
            src_file_.read(buffer.data(), to_read);
            size_t read_bytes = src_file_.gcount();

            if (read_bytes == 0) { break; }

            dst_file_.write(buffer.data(), read_bytes);

            if (!dst_file_.good()) {
                ti.error = Error{ErrorCode::DestinationWriteError, "Failed to write to destination",
                                 errno};
                ti.bytes_transferred = total_transferred;
                ti.completed = true;
                return;
            }

            total_transferred += read_bytes;
            remaining -= read_bytes;
        }

        ti.bytes_transferred = total_transferred;
        ti.error = Error{};
        ti.completed = true;
    }

    FileAddress src_;
    FileAddress dst_;
    SendfileProtocol protocol_;
    AnyAddress src_wrapped_;
    AnyAddress dst_wrapped_;

    std::ifstream src_file_;
    std::ofstream dst_file_;

    std::map<uint64_t, task_internal> tasks_;
    mutable std::mutex mutex_;
    std::atomic<bool> open_;
    std::atomic<uint64_t> next_task_id_;
};

REGISTER_TRANSFER_STREAM_WITH_PROTOCOL(LocalFile2LocalFileSendfileStream, FileAddress, FileAddress,
                                       SendfileProtocol)

}  // namespace ucm::transfer
