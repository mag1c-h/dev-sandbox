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
#ifndef MEMBW_BENCHMARK_BANDWIDTH_H
#define MEMBW_BENCHMARK_BANDWIDTH_H

#include <algorithm>
#include <chrono>
#include <numeric>
#include <vector>
#include "memcpy_instance.h"

class BandwidthBenchmark {
    using Clock = std::chrono::high_resolution_clock;
    using Duration = std::chrono::microseconds;

    size_t iterations_;
    size_t warmup_;

public:
    struct Result {
        std::string src;
        std::string dst;
        size_t elemSize;
        size_t elemCount;
        double minMs;
        double maxMs;
        double avgMs;
        double p50Ms;
        double p90Ms;
        double p99Ms;
    };

    BandwidthBenchmark(size_t iterations, size_t warmup) : iterations_(iterations), warmup_(warmup)
    {
    }
    Result Run(const MemcpyInstance& instance, const MemoryBuffer& src, MemoryBuffer& dst)
    {
        using namespace std::chrono;
        Result result;
        result.src = src.ReadMe();
        result.dst = dst.ReadMe();
        result.elemSize = src.Size();
        result.elemCount = src.Number();
        for (size_t i = 0; i < warmup_; ++i) { instance.Copy(src, dst); }
        std::vector<Duration> durations;
        for (size_t i = 0; i < iterations_; ++i) {
            auto start = Clock::now();
            instance.Copy(src, dst);
            auto end = Clock::now();
            durations.push_back(duration_cast<Duration>(end - start));
        }
        auto toMs = [](const Duration& d) { return d.count() / 1000.0; };
        std::sort(durations.begin(), durations.end());
        result.minMs = toMs(durations.front());
        result.maxMs = toMs(durations.back());
        result.avgMs = toMs(std::accumulate(durations.begin(), durations.end(), Duration(0)) /
                            durations.size());
        result.p50Ms = toMs(durations[durations.size() / 2]);
        result.p90Ms = toMs(durations[durations.size() * 9 / 10]);
        result.p99Ms = toMs(durations[durations.size() * 99 / 100]);
        return result;
    }
};

#endif  // MEMBW_BENCHMARK_BANDWIDTH_H
