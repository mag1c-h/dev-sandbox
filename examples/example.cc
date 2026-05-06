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
#include <iostream>
#include "transfer/transfer.h"

int main()
{
    const char* test_src = "/tmp/transfer_test_src.bin";
    const char* test_dst = "/tmp/transfer_test_dst.bin";

    std::ofstream out(test_src, std::ios::binary);
    for (int i = 0; i < 10000; ++i) { out.write(reinterpret_cast<const char*>(&i), sizeof(i)); }
    out.close();

    ucm::transfer::FileAddress src(test_src);
    ucm::transfer::FileAddress dst(test_dst);
    ucm::transfer::SendfileProtocol protocol{.chunk_size = 64 * 1024};

    auto create_result = ucm::transfer::Factory::create(src, dst, protocol);
    if (!create_result) {
        std::cerr << "Create failed: " << create_result.error().message << std::endl;
        return 1;
    }

    auto stream = create_result.take_value();

    auto r1 =
        stream->submit(ucm::transfer::IoTask{.ranges = {{.src = 0, .dst = 0, .size = 10000}}});
    if (!r1) {
        std::cerr << "Submit task 1 failed: " << r1.error().message << std::endl;
        stream->close();
        return 1;
    }
    uint64_t id1 = r1.take_value();
    std::cout << "Submitted task " << id1 << std::endl;

    auto r2 =
        stream->submit(ucm::transfer::IoTask{.ranges = {{.src = 10000, .dst = 0, .size = 20000}}});
    if (!r2) {
        std::cerr << "Submit task 2 failed: " << r2.error().message << std::endl;
        stream->close();
        return 1;
    }
    uint64_t id2 = r2.take_value();
    std::cout << "Submitted task " << id2 << std::endl;

    auto r3 =
        stream->submit(ucm::transfer::IoTask{.ranges = {{.src = 30000, .dst = 0, .size = 10000}}});
    if (!r3) {
        std::cerr << "Submit task 3 failed: " << r3.error().message << std::endl;
        stream->close();
        return 1;
    }
    uint64_t id3 = r3.take_value();
    std::cout << "Submitted task " << id3 << std::endl;

    std::cout << "Pending tasks: " << stream->pending_count() << std::endl;

    auto result1 = stream->synchronize(id1);
    if (!result1.ok()) {
        std::cerr << "Error: " << result1.error().message << std::endl;
        stream->close();
        return 1;
    }
    std::cout << "Task " << id1 << " completed: " << result1.value() << " bytes" << std::endl;

    std::cout << "Pending tasks after sync(id1): " << stream->pending_count() << std::endl;

    ucm::transfer::SyncResult sync_result = stream->synchronize();
    std::cout << "Sync completed: " << sync_result.total_tasks << " tasks"
              << ", " << sync_result.succeeded << " succeeded"
              << ", " << sync_result.bytes_total << " bytes" << std::endl;

    if (!sync_result.ok()) {
        std::cerr << "Error in task " << sync_result.first_failed_task_id << ": "
                  << sync_result.first_error.message << std::endl;
        stream->close();
        return 1;
    }

    std::cout << "Pending tasks after sync(): " << stream->pending_count() << std::endl;

    stream->close();
    std::cout << "Stream closed" << std::endl;

    std::ifstream check_dst(test_dst, std::ios::binary);
    check_dst.seekg(0, std::ios::end);
    std::cout << "Destination file size: " << check_dst.tellg() << " bytes" << std::endl;
    check_dst.close();

    std::cout << "Transfer successful!" << std::endl;

    return 0;
}
