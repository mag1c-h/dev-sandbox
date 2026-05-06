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
#include <tuple>
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

    Expected<void> submit(uint64_t src, uint64_t dst, std::size_t size) override
    {
        if (!src_file_.is_open() || !dst_file_.is_open()) {
            return Error{ErrorCode::StreamClosed, "File streams not open"};
        }

        if (size == 0) { return Error{ErrorCode::InvalidTask, "Size is 0"}; }

        std::lock_guard<std::mutex> lock(mutex_);
        ranges_.emplace_back(src, dst, size);
        return Expected<void>();
    }

    Expected<void> submit(std::vector<std::tuple<uint64_t, uint64_t, std::size_t>> ranges) override
    {
        if (!src_file_.is_open() || !dst_file_.is_open()) {
            return Error{ErrorCode::StreamClosed, "File streams not open"};
        }

        if (ranges.empty()) { return Error{ErrorCode::InvalidTask, "Ranges is empty"}; }

        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& range : ranges) { ranges_.push_back(range); }
        return Expected<void>();
    }

    Expected<void> synchronize() override
    {
        if (!src_file_.is_open() || !dst_file_.is_open()) {
            return Error{ErrorCode::StreamClosed, "File streams not open"};
        }

        std::vector<std::tuple<uint64_t, uint64_t, std::size_t>> ranges;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ranges = std::move(ranges_);
            ranges_.clear();
        }

        for (const auto& range : ranges) {
            auto result = execute_range(range);
            if (!result.ok()) { return result.error(); }
        }

        return Expected<void>();
    }

private:
    Expected<void> execute_range(const std::tuple<uint64_t, uint64_t, std::size_t>& range)
    {
        if (!src_file_.is_open() || !dst_file_.is_open()) {
            return Error{ErrorCode::SourceNotFound, "File not open"};
        }

        uint64_t src_offset = std::get<0>(range);
        uint64_t dst_offset = std::get<1>(range);
        std::size_t size = std::get<2>(range);

        if (size == 0) { return Error{ErrorCode::InvalidTask, "Range size is 0"}; }

        std::vector<char> buffer(protocol_.chunk_size);

        src_file_.seekg(src_offset);
        dst_file_.seekp(dst_offset);

        std::size_t remaining = size;

        while (remaining > 0 && !src_file_.eof() && src_file_.good() && dst_file_.good()) {
            std::size_t to_read = std::min(remaining, protocol_.chunk_size);
            src_file_.read(buffer.data(), to_read);
            std::size_t read_bytes = src_file_.gcount();

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

    std::vector<std::tuple<uint64_t, uint64_t, std::size_t>> ranges_;
    mutable std::mutex mutex_;
};

REGISTER_TRANSFER_STREAM_WITH_PROTOCOL(LocalFile2LocalFileSendfileStream, FileAddress, FileAddress,
                                       SendfileProtocol)

}  // namespace ucm::transfer
