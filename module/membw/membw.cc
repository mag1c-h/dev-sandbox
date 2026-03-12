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
#include <fmt/format.h>
#include <memory>
#include "benchmark_bandwidth.h"

struct MemcpyBandwidthTestCase {
    std::shared_ptr<MemcpyInstance> instance;
    std::shared_ptr<MemoryBuffer> src;
    std::shared_ptr<MemoryBuffer> dst;
};

std::vector<std::shared_ptr<MemoryBuffer>> MakeMemoryBuffers(size_t bufferSize, size_t bufferNumber)
{
    std::vector<std::shared_ptr<MemoryBuffer>> buffers;
    const auto pair = 2;
    for (size_t i = 0; i < pair; ++i) {
        buffers.push_back(std::make_shared<MmapAnonymousBuffer>(bufferSize, bufferNumber));
        buffers.push_back(std::make_shared<MmapSharedBuffer>(bufferSize, bufferNumber));
        buffers.push_back(std::make_shared<PosixMemalignBuffer>(bufferSize, bufferNumber, 4096));
        buffers.push_back(
            std::make_shared<PosixMemalignBuffer>(bufferSize, bufferNumber, 1024 * 1024));
    }
    return buffers;
}

std::vector<MemcpyBandwidthTestCase> CreateTestCases(
    const std::vector<std::shared_ptr<MemoryBuffer>>& buffers)
{
    auto standardInstance = std::make_shared<StandardMemcpyInstance>();
    std::vector<MemcpyBandwidthTestCase> testCases;
    for (const auto& src : buffers) {
        for (const auto& dst : buffers) {
            if (src.get() == dst.get()) { continue; }
            testCases.push_back({standardInstance, src, dst});
        }
    }
    return testCases;
}

void Evaluate(const std::vector<MemcpyBandwidthTestCase>& items, size_t iterations, size_t warmup)
{
    fmt::println("{:<30}{:<30}{:<10}{:<12}{:<12}{:<12}{:<12}{:<12}{:<12}{:<12}{:<12}", "From", "To",
                 "Count", "Size(KB)", "Min(ms)", "Max(ms)", "Avg(ms)", "P50(ms)", "P90(ms)",
                 "P99(ms)", "BW(GB/s)");
    BandwidthBenchmark benchmark(iterations, warmup);
    for (const auto& item : items) {
        auto result = benchmark.Run(*item.instance, *item.src, *item.dst);
        double bandwidth = (result.elemSize * result.elemCount) / (result.avgMs * 1e6);
        fmt::println(
            "{:<30}{:<30}{:<10}{:<12}{:<12.3f}{:<12.3f}{:<12.3f}{:<12.3f}{:<12.3f}{:<12.3f}{:<"
            "12.3f}",
            result.src, result.dst, result.elemCount, result.elemSize / 1024.0, result.minMs,
            result.maxMs, result.avgMs, result.p50Ms, result.p90Ms, result.p99Ms, bandwidth);
    }
}

int main(int argc, char const* argv[])
{
    constexpr size_t bufferSize = 128 * 1024;
    constexpr size_t bufferNumber = 128;
    constexpr size_t iterations = 1024;
    constexpr size_t warmup = 3;

    auto buffers = MakeMemoryBuffers(bufferSize, bufferNumber);
    auto testCases = CreateTestCases(buffers);
    Evaluate(testCases, iterations, warmup);

    return 0;
}
