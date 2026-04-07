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
#ifndef COPY_CASE_H
#define COPY_CASE_H

#include "copy_instance.h"
#include "copy_result.h"

class CopyCase {
    std::string key_;
    std::string brief_;

public:
    CopyCase(std::string key, std::string brief) : key_{std::move(key)}, brief_{std::move(brief)} {}
    virtual ~CopyCase() = default;
    virtual void Run(size_t size, size_t num, size_t iter, size_t nDevice) const = 0;
    const std::string& Key() const { return key_; }
    const std::string& Brief() const { return brief_; }
};

class Host2DeviceCECase : public CopyCase {
public:
    Host2DeviceCECase()
        : CopyCase{"host_to_device_ce", "memcpy from host to device with ce one by one"}
    {
    }
    void Run(size_t size, size_t num, size_t iter, size_t nDevice) const override
    {
        CudaMemcpyHost2DeviceCopyInitiator initiator;
        CopyInstance instance{&initiator, iter, false};
        CopyResult result;
        for (size_t device = 0; device < nDevice; device++) {
            CudaHostCopyBuffer srcBuffer{device, size, num};
            CudaDeviceCopyBuffer dstBuffer{device, size, num};
            result.Push(instance.DoCopy(&srcBuffer, &dstBuffer));
        }
        result.Show("[[ " + Key() + " ]] " + Brief());
    }
};

class Host2DeviceSMCase : public CopyCase {
public:
    Host2DeviceSMCase()
        : CopyCase{"host_to_device_sm", "memcpy from host to device with sm one by one"}
    {
    }
    void Run(size_t size, size_t num, size_t iter, size_t nDevice) const override
    {
        CopyResult result;
        for (size_t device = 0; device < nDevice; device++) {
            CudaHostCopyBuffer srcBuffer{device, size, num};
            CudaDeviceCopyBuffer dstBuffer{device, size, num};
            CudaSMBatchCopyInitiator initiator(device, num);
            CopyInstance instance{&initiator, iter, false};
            result.Push(instance.DoCopy(&srcBuffer, &dstBuffer));
        }
        result.Show("[[ " + Key() + " ]] " + Brief());
    }
};

class OneHost2AllDeviceCECase : public CopyCase {
public:
    OneHost2AllDeviceCECase()
        : CopyCase{"one_host_to_all_device_ce", "memcpy from one host to all device with ce"}
    {
    }
    void Run(size_t size, size_t num, size_t iter, size_t nDevice) const override
    {
        CudaMemcpyHost2DeviceCopyInitiator initiator;
        CopyInstance instance{&initiator, iter, false};
        CopyResult result;
        CudaHostCopyBuffer srcBuffer{0, size, num};
        for (size_t device = 0; device < nDevice; device++) {
            CudaDeviceCopyBuffer dstBuffer{device, size, num};
            result.Push(instance.DoCopy(&srcBuffer, &dstBuffer));
        }
        result.Show("[[ " + Key() + " ]] " + Brief());
    }
};

class OneHost2AllDeviceSMCase : public CopyCase {
public:
    OneHost2AllDeviceSMCase()
        : CopyCase{"one_host_to_all_device_sm", "memcpy from one host to all device with sm"}
    {
    }
    void Run(size_t size, size_t num, size_t iter, size_t nDevice) const override
    {
        CopyResult result;
        CudaHostCopyBuffer srcBuffer{0, size, num};
        for (size_t device = 0; device < nDevice; device++) {
            CudaDeviceCopyBuffer dstBuffer{device, size, num};
            CudaSMBatchCopyInitiator initiator{device, num};
            CopyInstance instance{&initiator, iter, false};
            result.Push(instance.DoCopy(&srcBuffer, &dstBuffer));
        }
        result.Show("[[ " + Key() + " ]] " + Brief());
    }
};

class AllHost2AllDeviceCECase : public CopyCase {
public:
    AllHost2AllDeviceCECase()
        : CopyCase{"all_host_to_all_device_ce",
                   "memcpy from all host to all device with ce at one time"}
    {
    }
    void Run(size_t size, size_t num, size_t iter, size_t nDevice) const override
    {
        CudaMemcpyHost2DeviceCopyInitiator initiator;
        CopyInstance instance{&initiator, iter, false};
        std::vector<const CopyBuffer*> srcBuffers(nDevice);
        std::vector<const CopyBuffer*> dstBuffers(nDevice);
        for (size_t device = 0; device < nDevice; device++) {
            srcBuffers[device] = new CudaHostCopyBuffer{device, size, num};
            dstBuffers[device] = new CudaDeviceCopyBuffer{device, size, num};
        }
        CopyResult result;
        result.Push(instance.DoCopy(srcBuffers, dstBuffers));
        for (size_t device = 0; device < nDevice; device++) {
            delete srcBuffers[device];
            delete dstBuffers[device];
        }
        result.Show("[[ " + Key() + " ]] " + Brief());
    }
};

class Device2DeviceCECase : public CopyCase {
public:
    Device2DeviceCECase()
        : CopyCase{"device_to_device_ce", "memcpy from device to device with ce one by one"}
    {
    }
    void Run(size_t size, size_t num, size_t iter, size_t nDevice) const override
    {
        CudaMemcpyDevice2DeviceCopyInitiator initiator;
        CopyInstance instance{&initiator, iter, false};
        CopyResult result;
        for (size_t device = 0; device < nDevice; device++) {
            CudaDeviceCopyBuffer srcBuffer{device, size, num};
            CudaDeviceCopyBuffer dstBuffer{device, size, num};
            result.Push(instance.DoCopy(&srcBuffer, &dstBuffer));
        }
        result.Show("[[ " + Key() + " ]] " + Brief());
    }
};

class OneDevice2AllDeviceCECase : public CopyCase {
public:
    OneDevice2AllDeviceCECase()
        : CopyCase{"one_device_to_all_device_ce", "memcpy from one device to all device with ce"}
    {
    }
    void Run(size_t size, size_t num, size_t iter, size_t nDevice) const override
    {
        CudaMemcpyDevice2DeviceCopyInitiator initiator;
        CopyInstance instance{&initiator, iter, false};
        CopyResult result;
        CudaDeviceCopyBuffer srcBuffer{0, size, num};
        for (size_t device = 0; device < nDevice; device++) {
            CudaDeviceCopyBuffer dstBuffer{device, size, num};
            result.Push(instance.DoCopy(&srcBuffer, &dstBuffer));
        }
        result.Show("[[ " + Key() + " ]] " + Brief());
    }
};

#endif
