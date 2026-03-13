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
#ifndef NVBW_MEMCPY_CASE_H
#define NVBW_MEMCPY_CASE_H

#include "memcpy_instance.h"
#include "memcpy_result.h"

class MemcpyParameterSet {
    MemcpyParameterSet() = default;
    MemcpyParameterSet(const MemcpyParameterSet&) = delete;
    MemcpyParameterSet& operator=(const MemcpyParameterSet&) = delete;
    MemcpyParameterSet(MemcpyParameterSet&&) = delete;
    MemcpyParameterSet& operator=(MemcpyParameterSet&&) = delete;

public:
    static MemcpyParameterSet& Instance()
    {
        static MemcpyParameterSet set;
        return set;
    }

    int32_t deviceNumber;
    size_t bufferSize;
    size_t bufferNumber;
    size_t iterations = 1024;
    size_t warmup = 3;
};

class MemcpyCase {
    std::string key_;

public:
    MemcpyCase(std::string key) : key_(std::move(key)) {}
    virtual ~MemcpyCase() = default;
    virtual void Run() = 0;
    const std::string& Key() const noexcept { return key_; }
};

class HostToDeviceMemcpyCase : public MemcpyCase {
public:
    HostToDeviceMemcpyCase() : MemcpyCase("host_to_device_memcpy_ce") {}
    void Run() override
    {
        auto& param = MemcpyParameterSet::Instance();
        Host2DeviceCEMemcpyInitiator initiator;
        MemcpyInstance memcpyInstance{param.iterations, param.warmup, &initiator};
        MemcpyResult result;
        for (auto deviceId = 0; deviceId < param.deviceNumber; deviceId++) {
            CudaHostMemoryBuffer srcBuffer{deviceId, param.bufferSize, param.bufferNumber};
            CudaDeviceMemoryBuffer dstBuffer{deviceId, param.bufferSize, param.bufferNumber};
            result.Record(memcpyInstance.DoMemcpy(srcBuffer, dstBuffer));
        }
        result.Show("memcpy CE CPU -> GPU(row) bandwidth");
    }
};

class Host0ToDeviceMemcpyCase : public MemcpyCase {
public:
    Host0ToDeviceMemcpyCase() : MemcpyCase("host0_to_device_memcpy_ce") {}
    void Run() override
    {
        auto& param = MemcpyParameterSet::Instance();
        Host2DeviceCEMemcpyInitiator initiator;
        MemcpyInstance memcpyInstance{param.iterations, param.warmup, &initiator};
        MemcpyResult result;
        CudaHostMemoryBuffer srcBuffer{0, param.bufferSize, param.bufferNumber};
        for (auto deviceId = 0; deviceId < param.deviceNumber; deviceId++) {
            CudaDeviceMemoryBuffer dstBuffer{deviceId, param.bufferSize, param.bufferNumber};
            result.Record(memcpyInstance.DoMemcpy(srcBuffer, dstBuffer));
        }
        result.Show("memcpy CE CPU(0) -> GPU(row) bandwidth");
    }
};

class HostToAllDeviceMemcpyCase : public MemcpyCase {
public:
    HostToAllDeviceMemcpyCase() : MemcpyCase("host_to_all_device_memcpy_ce") {}
    void Run() override
    {
        auto& param = MemcpyParameterSet::Instance();
        Host2DeviceCEMemcpyInitiator initiator;
        MemcpyInstance memcpyInstance{param.iterations, param.warmup, &initiator};
        MemcpyResult result;
        std::vector<const MemoryBuffer*> srcBuffers(param.deviceNumber);
        std::vector<const MemoryBuffer*> dstBuffers(param.deviceNumber);
        CudaHostMemoryBuffer srcBuffer(0, param.bufferSize, param.bufferNumber);
        for (auto deviceId = 0; deviceId < param.deviceNumber; deviceId++) {
            auto dstBuffer =
                new CudaDeviceMemoryBuffer(deviceId, param.bufferSize, param.bufferNumber);
            srcBuffers[deviceId] = &srcBuffer;
            dstBuffers[deviceId] = dstBuffer;
        }
        result.Record(memcpyInstance.DoMemcpy(srcBuffers, dstBuffers));
        result.Show("memcpy CE CPU -> GPU(all) bandwidth");
        for (auto& buffer : dstBuffers) { delete buffer; }
    }
};

class AllHostToAllDeviceMemcpyCase : public MemcpyCase {
public:
    AllHostToAllDeviceMemcpyCase() : MemcpyCase("all_host_to_all_device_memcpy_ce") {}
    void Run() override
    {
        auto& param = MemcpyParameterSet::Instance();
        Host2DeviceCEMemcpyInitiator initiator;
        MemcpyInstance memcpyInstance{param.iterations, param.warmup, &initiator};
        MemcpyResult result;
        std::vector<const MemoryBuffer*> srcBuffers(param.deviceNumber);
        std::vector<const MemoryBuffer*> dstBuffers(param.deviceNumber);
        for (auto deviceId = 0; deviceId < param.deviceNumber; deviceId++) {
            srcBuffers[deviceId] =
                new CudaHostMemoryBuffer(deviceId, param.bufferSize, param.bufferNumber);
            dstBuffers[deviceId] =
                new CudaDeviceMemoryBuffer(deviceId, param.bufferSize, param.bufferNumber);
        }
        result.Record(memcpyInstance.DoMemcpy(srcBuffers, dstBuffers));
        result.Show("memcpy CE CPU(all) -> GPU(all) bandwidth");
        for (auto& buffer : srcBuffers) { delete buffer; }
        for (auto& buffer : dstBuffers) { delete buffer; }
    }
};

class MmapToDeviceMemcpyCase : public MemcpyCase {
public:
    MmapToDeviceMemcpyCase() : MemcpyCase("mmap_to_device_memcpy_ce") {}
    void Run() override
    {
        auto& param = MemcpyParameterSet::Instance();
        Host2DeviceCEMemcpyInitiator initiator;
        MemcpyInstance memcpyInstance{param.iterations, param.warmup, &initiator};
        MemcpyResult result;
        for (auto deviceId = 0; deviceId < param.deviceNumber; deviceId++) {
            MmapSharedRegisteredBuffer srcBuffer{deviceId, param.bufferSize, param.bufferNumber};
            CudaDeviceMemoryBuffer dstBuffer{deviceId, param.bufferSize, param.bufferNumber};
            result.Record(memcpyInstance.DoMemcpy(srcBuffer, dstBuffer));
        }
        result.Show("memcpy CE CPU(mmap) -> GPU(row) bandwidth");
    }
};

class Mmap0ToDeviceMemcpyCase : public MemcpyCase {
public:
    Mmap0ToDeviceMemcpyCase() : MemcpyCase("mmap0_to_device_memcpy_ce") {}
    void Run() override
    {
        auto& param = MemcpyParameterSet::Instance();
        Host2DeviceCEMemcpyInitiator initiator;
        MemcpyInstance memcpyInstance{param.iterations, param.warmup, &initiator};
        MemcpyResult result;
        MmapSharedRegisteredBuffer srcBuffer{0, param.bufferSize, param.bufferNumber};
        for (auto deviceId = 0; deviceId < param.deviceNumber; deviceId++) {
            CudaDeviceMemoryBuffer dstBuffer{deviceId, param.bufferSize, param.bufferNumber};
            result.Record(memcpyInstance.DoMemcpy(srcBuffer, dstBuffer));
        }
        result.Show("memcpy CE CPU(mmap0) -> GPU(row) bandwidth");
    }
};

class MmapToAllDeviceMemcpyCase : public MemcpyCase {
public:
    MmapToAllDeviceMemcpyCase() : MemcpyCase("mmap_to_all_device_memcpy_ce") {}
    void Run() override
    {
        auto& param = MemcpyParameterSet::Instance();
        Host2DeviceCEMemcpyInitiator initiator;
        MemcpyInstance memcpyInstance{param.iterations, param.warmup, &initiator};
        MemcpyResult result;
        const char* shmName = "nvbw_shared_buffer";
        std::vector<const MemoryBuffer*> srcBuffers(param.deviceNumber);
        std::vector<const MemoryBuffer*> dstBuffers(param.deviceNumber);
        for (auto deviceId = 0; deviceId < param.deviceNumber; deviceId++) {
            srcBuffers[deviceId] = new MmapSharedRegisteredBuffer(
                shmName, deviceId, param.bufferSize, param.bufferNumber);
            dstBuffers[deviceId] =
                new CudaDeviceMemoryBuffer(deviceId, param.bufferSize, param.bufferNumber);
        }
        result.Record(memcpyInstance.DoMemcpy(srcBuffers, dstBuffers));
        result.Show("memcpy CE CPU(mmap) -> GPU(all) bandwidth");
        for (auto& buffer : srcBuffers) { delete buffer; }
        for (auto& buffer : dstBuffers) { delete buffer; }
        shm_unlink(shmName);
    }
};

#endif  // NVBW_MEMCPY_CASE_H
