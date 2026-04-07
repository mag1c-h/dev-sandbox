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
public:
    virtual ~CopyCase() = default;
    virtual void Run() const = 0;
};

template <size_t Size, size_t Num, size_t Iter>
class Host2DeviceCECopyCase : public CopyCase {
    std::string name_;
    size_t deviceNumber_;
    size_t warmup_;

public:
    explicit Host2DeviceCECopyCase(std::string name, size_t deviceNumber = 8, size_t warmup = 3)
        : CopyCase{}, name_{std::move(name)}, deviceNumber_{deviceNumber}, warmup_{warmup}
    {
    }
    void Run() const override
    {
        CudaMemcpyHost2DeviceCopyInitiator initiator;
        CopyInstance instance{&initiator, warmup_, Iter, false};
        CopyResult result;
        for (size_t device = 0; device < deviceNumber_; device++) {
            CudaHostCopyBuffer srcBuffer{device, Size, Num};
            CudaDeviceCopyBuffer dstBuffer{device, Size, Num};
            result.Push(instance.DoCopy(&srcBuffer, &dstBuffer));
        }
        result.Show("[[ " + name_ + " ]] - One to One");
    }
};

template <size_t Size, size_t Num, size_t Iter>
class Host2DeviceSMCopyCase : public CopyCase {
    std::string name_;
    size_t deviceNumber_;
    size_t warmup_;

public:
    explicit Host2DeviceSMCopyCase(std::string name, size_t deviceNumber = 8, size_t warmup = 3)
        : CopyCase{}, name_{std::move(name)}, deviceNumber_{deviceNumber}, warmup_{warmup}
    {
    }
    void Run() const override
    {
        CopyResult result;
        for (size_t device = 0; device < deviceNumber_; device++) {
            CudaHostCopyBuffer srcBuffer{device, Size, Num};
            CudaDeviceCopyBuffer dstBuffer{device, Size, Num};
            CudaSMBatchCopyInitiator initiator(device, Num);
            CopyInstance instance{&initiator, warmup_, Iter, false};
            result.Push(instance.DoCopy(&srcBuffer, &dstBuffer));
        }
        result.Show("[[ " + name_ + " ]] - One to One");
    }
};

template <size_t Size, size_t Num, size_t Iter>
class OneHost2AllDeviceCECopyCase : public CopyCase {
    std::string name_;
    size_t deviceNumber_;
    size_t warmup_;

public:
    explicit OneHost2AllDeviceCECopyCase(std::string name, size_t deviceNumber = 8,
                                         size_t warmup = 3)
        : CopyCase{}, name_{std::move(name)}, deviceNumber_{deviceNumber}, warmup_{warmup}
    {
    }
    void Run() const override
    {
        CudaMemcpyHost2DeviceCopyInitiator initiator;
        CopyInstance instance{&initiator, warmup_, Iter, false};
        CopyResult result;
        CudaHostCopyBuffer srcBuffer{0, Size, Num};
        for (size_t device = 0; device < deviceNumber_; device++) {
            CudaDeviceCopyBuffer dstBuffer{device, Size, Num};
            result.Push(instance.DoCopy(&srcBuffer, &dstBuffer));
        }
        result.Show("[[ " + name_ + " ]] - One to All");
    }
};

template <size_t Size, size_t Num, size_t Iter>
class AllHost2AllDeviceCECopyCase : public CopyCase {
    std::string name_;
    size_t deviceNumber_;
    size_t warmup_;

public:
    explicit AllHost2AllDeviceCECopyCase(std::string name, size_t deviceNumber = 8,
                                         size_t warmup = 3)
        : CopyCase{}, name_{std::move(name)}, deviceNumber_{deviceNumber}, warmup_{warmup}
    {
    }
    void Run() const override
    {
        CudaMemcpyHost2DeviceCopyInitiator initiator;
        CopyInstance instance{&initiator, warmup_, Iter, false};
        std::vector<const CopyBuffer*> srcBuffers(deviceNumber_);
        std::vector<const CopyBuffer*> dstBuffers(deviceNumber_);
        for (size_t device = 0; device < deviceNumber_; device++) {
            srcBuffers[device] = new CudaHostCopyBuffer{device, Size, Num};
            dstBuffers[device] = new CudaDeviceCopyBuffer{device, Size, Num};
        }
        CopyResult result;
        result.Push(instance.DoCopy(srcBuffers, dstBuffers));
        for (size_t device = 0; device < deviceNumber_; device++) {
            delete srcBuffers[device];
            delete dstBuffers[device];
        }
        result.Show("[[ " + name_ + " ]] - All to All");
    }
};

template <size_t Size, size_t Num, size_t Iter>
class Device2DeviceCECopyCase : public CopyCase {
    std::string name_;
    size_t deviceNumber_;
    size_t warmup_;

public:
    explicit Device2DeviceCECopyCase(std::string name, size_t deviceNumber = 8, size_t warmup = 3)
        : CopyCase{}, name_{std::move(name)}, deviceNumber_{deviceNumber}, warmup_{warmup}
    {
    }
    void Run() const override
    {
        CudaMemcpyDevice2DeviceCopyInitiator initiator;
        CopyInstance instance{&initiator, warmup_, Iter, false};
        CopyResult result;
        for (size_t device = 0; device < deviceNumber_; device++) {
            CudaDeviceCopyBuffer srcBuffer{device, Size, Num};
            CudaDeviceCopyBuffer dstBuffer{device, Size, Num};
            result.Push(instance.DoCopy(&srcBuffer, &dstBuffer));
        }
        result.Show("[[ " + name_ + " ]] - One to One");
    }
};

template <size_t Size, size_t Num, size_t Iter>
class OneDevice2AllDeviceCECopyCase : public CopyCase {
    std::string name_;
    size_t deviceNumber_;
    size_t warmup_;

public:
    explicit OneDevice2AllDeviceCECopyCase(std::string name, size_t deviceNumber = 8,
                                           size_t warmup = 3)
        : CopyCase{}, name_{std::move(name)}, deviceNumber_{deviceNumber}, warmup_{warmup}
    {
    }
    void Run() const override
    {
        CudaMemcpyDevice2DeviceCopyInitiator initiator;
        CopyInstance instance{&initiator, warmup_, Iter, false};
        CopyResult result;
        CudaDeviceCopyBuffer srcBuffer{0, Size, Num};
        for (size_t device = 0; device < deviceNumber_; device++) {
            CudaDeviceCopyBuffer dstBuffer{device, Size, Num};
            result.Push(instance.DoCopy(&srcBuffer, &dstBuffer));
        }
        result.Show("[[ " + name_ + " ]] - One to All");
    }
};

#endif
