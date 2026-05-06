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
#include <fstream>
#include <mutex>
#include <vector>
#include "address/local_file.h"
#include "protocol/sendfile.h"
#include "transfer/abstract/registry.h"
#include "transfer/abstract/stream.h"

namespace ucm::transfer {

class LocalFile2LocalFileSendfileStream : public IStream {
public:
    LocalFile2LocalFileSendfileStream(FileAddress src, FileAddress dst, SendfileProtocol protocol)
        : src_(std::move(src)), dst_(std::move(dst)), protocol_(protocol)
    {
        src_file_.open(src_.path, std::ios::binary | std::ios::in);
        dst_file_.open(dst_.path, std::ios::binary | std::ios::out);
    }

    ~LocalFile2LocalFileSendfileStream() override
    {
        src_file_.close();
        dst_file_.close();
    }

    Expected<void> submit(IoTask task) override
    {
        if (!src_file_.is_open() || !dst_file_.is_open()) {
            return Error{ErrorCode::StreamClosed, "File streams not open"};
        }

        if (task.ranges.empty()) { return Error{ErrorCode::InvalidTask, "IoTask ranges is empty"}; }

        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push_back(std::move(task));
        return Expected<void>();
    }

    Expected<void> synchronize() override
    {
        if (!src_file_.is_open() || !dst_file_.is_open()) {
            return Error{ErrorCode::StreamClosed, "File streams not open"};
        }

        std::vector<IoTask> tasks;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks = std::move(tasks_);
            tasks_.clear();
        }

        for (const auto& task : tasks) {
            for (const auto& range : task.ranges) {
                auto result = execute_range(range);
                if (!result.ok()) { return result.error(); }
            }
        }

        return Expected<void>();
    }

private:
    Expected<void> execute_range(const IoTask::Range& range)
    {
        if (!src_file_.is_open() || !dst_file_.is_open()) {
            return Error{ErrorCode::SourceNotFound, "File not open"};
        }

        if (range.size == 0) { return Error{ErrorCode::InvalidTask, "Range size is 0"}; }

        std::vector<char> buffer(protocol_.chunk_size);

        src_file_.seekg(range.src);
        dst_file_.seekp(range.dst);

        size_t remaining = range.size;

        while (remaining > 0 && !src_file_.eof() && src_file_.good() && dst_file_.good()) {
            size_t to_read = std::min(remaining, protocol_.chunk_size);
            src_file_.read(buffer.data(), to_read);
            size_t read_bytes = src_file_.gcount();

            if (read_bytes == 0) { break; }

            dst_file_.write(buffer.data(), read_bytes);

            if (!dst_file_.good()) {
                return Error{ErrorCode::DestinationWriteError, "Failed to write to destination",
                             errno};
            }

            remaining -= read_bytes;
        }

        if (remaining > 0 && src_file_.eof()) {
            return Error{ErrorCode::SourceEof,
                         "Source file reached EOF before completing transfer"};
        }

        return Expected<void>();
    }

    FileAddress src_;
    FileAddress dst_;
    SendfileProtocol protocol_;

    std::ifstream src_file_;
    std::ofstream dst_file_;

    std::vector<IoTask> tasks_;
    mutable std::mutex mutex_;
};

REGISTER_TRANSFER_STREAM_WITH_PROTOCOL(LocalFile2LocalFileSendfileStream, FileAddress, FileAddress,
                                       SendfileProtocol)

}  // namespace ucm::transfer
