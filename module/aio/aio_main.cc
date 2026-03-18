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
#include <chrono>
#include <random>
#include "aio_engine.h"
#include "host_buffer.h"

std::vector<aio::BlockId> MakeBlockIdsRandomly(size_t number)
{
    auto makeBlockIdRandomly = +[] {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<std::uint8_t> dist(0, 255);
        aio::BlockId id;
        for (std::size_t i = 0; i < id.size(); ++i) { id[i] = static_cast<std::byte>(dist(gen)); }
        return id;
    };
    std::vector<aio::BlockId> blockIds;
    blockIds.reserve(number);
    for (size_t i = 0; i < number; i++) { blockIds.push_back(makeBlockIdRandomly()); }
    return blockIds;
}

template <bool write>
void Run(aio::AioEngine& ioEngine, aio::AioEngine::IoTask& task, size_t epochNumber)
{
    using namespace std::chrono;
    const char* taskType = write ? "Write" : "Read";
    const auto size = task.front().length;
    const auto number = task.size();
    const auto total = size * number;
    for (size_t i = 0; i < epochNumber; i++) {
        auto tp = steady_clock::now();
        if constexpr (write) {
            ioEngine.SubmitWrite(task)->Wait();
        } else {
            ioEngine.SubmitRead(task)->Wait();
        }
        auto cost = duration<double>(steady_clock::now() - tp).count() * 1e3;
        auto bandwidth = total / cost / 1e6;
        fmt::println("[{:04}/{:04}] {} task({} x {}) finish, cost={:.3f}ms, bw={:.3f}GB/s.", i + 1,
                     epochNumber, taskType, size, number, cost, bandwidth);
    }
}

int main(int argc, char const* argv[])
{
    const char* workspace = "./build/data";
    const auto ioType = aio::HostBuffer::Strategy::MMAP;
    const size_t ioSize = 1024 * 1024;
    const size_t ioNumber = 512;
    const size_t deviceId = 0;
    const size_t epochNumber = 32;

    aio::SpaceLayout layout{std::string(workspace)};
    aio::AioEngine ioEngine{&layout, 32};
    aio::HostBuffer buffers{ioType, deviceId, ioSize, ioNumber};
    auto blockIds = MakeBlockIdsRandomly(ioNumber);
    aio::AioEngine::IoTask task;
    task.reserve(ioNumber);
    for (size_t i = 0; i < ioNumber; i++) {
        task.push_back(aio::AioEngine::IoShard{blockIds[i], buffers[i], ioSize});
    }

    Run<true>(ioEngine, task, epochNumber);
    Run<false>(ioEngine, task, epochNumber);

    return 0;
}
