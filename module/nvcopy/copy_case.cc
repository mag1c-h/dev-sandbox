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
#include "copy_case.h"

template <typename T>
class Registrar {
public:
    Registrar() { CopyCaseFactory::Factory().Register(std::make_shared<T>()); }
};

#define DEFINE_COPY_CASE(ClassName, Key, Brief, Ctx)            \
    class ClassName : public CopyCase {                         \
    public:                                                     \
        ClassName() : CopyCase(Key, Brief) {}                   \
        void Run(const Context&) const override;                \
    };                                                          \
    static Registrar<ClassName> global_##ClassName##_registrar; \
    void ClassName::Run(const Context& Ctx) const

DEFINE_COPY_CASE(Host2DeviceCECase, "host_to_device_ce",
                 "memcpy from host to device with ce one by one", ctx)
{
    CudaMemcpyHost2DeviceCopyInitiator initiator;
    CopyInstance instance{&initiator, ctx.iter, false};
    CopyResult result;
    for (size_t device = 0; device < ctx.nDevice; device++) {
        CudaHostCopyBuffer srcBuffer{device, ctx.size, ctx.num};
        CudaDeviceCopyBuffer dstBuffer{device, ctx.size, ctx.num};
        result.Push(instance.DoCopy(&srcBuffer, &dstBuffer));
    }
    result.Show("[[ " + Key() + " ]] " + Brief());
}

DEFINE_COPY_CASE(Host2DeviceSMCase, "host_to_device_sm",
                 "memcpy from host to device with sm one by one", ctx)
{
    CopyResult result;
    for (size_t device = 0; device < ctx.nDevice; device++) {
        CudaHostCopyBuffer srcBuffer{device, ctx.size, ctx.num};
        CudaDeviceCopyBuffer dstBuffer{device, ctx.size, ctx.num};
        CudaSMBatchCopyInitiator initiator(device, ctx.num);
        CopyInstance instance{&initiator, ctx.iter, false};
        result.Push(instance.DoCopy(&srcBuffer, &dstBuffer));
    }
    result.Show("[[ " + Key() + " ]] " + Brief());
}

DEFINE_COPY_CASE(OneHost2AllDeviceCECase, "one_host_to_all_device_ce",
                 "memcpy from one host to all device with ce", ctx)
{
    CudaMemcpyHost2DeviceCopyInitiator initiator;
    CopyInstance instance{&initiator, ctx.iter, false};
    CopyResult result;
    CudaHostCopyBuffer srcBuffer{0, ctx.size, ctx.num};
    for (size_t device = 0; device < ctx.nDevice; device++) {
        CudaDeviceCopyBuffer dstBuffer{device, ctx.size, ctx.num};
        result.Push(instance.DoCopy(&srcBuffer, &dstBuffer));
    }
    result.Show("[[ " + Key() + " ]] " + Brief());
}

DEFINE_COPY_CASE(OneHost2AllDeviceSMCase, "one_host_to_all_device_sm",
                 "memcpy from one host to all device with sm", ctx)
{
    CopyResult result;
    CudaHostCopyBuffer srcBuffer{0, ctx.size, ctx.num};
    for (size_t device = 0; device < ctx.nDevice; device++) {
        CudaDeviceCopyBuffer dstBuffer{device, ctx.size, ctx.num};
        CudaSMBatchCopyInitiator initiator{device, ctx.num};
        CopyInstance instance{&initiator, ctx.iter, false};
        result.Push(instance.DoCopy(&srcBuffer, &dstBuffer));
    }
    result.Show("[[ " + Key() + " ]] " + Brief());
}

DEFINE_COPY_CASE(AllHost2AllDeviceCECase, "all_host_to_all_device_ce",
                 "memcpy from all host to all device with ce at one time", ctx)
{
    CudaMemcpyHost2DeviceCopyInitiator initiator;
    CopyInstance instance{&initiator, ctx.iter, false};
    std::vector<const CopyBuffer*> srcBuffers(ctx.nDevice);
    std::vector<const CopyBuffer*> dstBuffers(ctx.nDevice);
    for (size_t device = 0; device < ctx.nDevice; device++) {
        srcBuffers[device] = new CudaHostCopyBuffer{device, ctx.size, ctx.num};
        dstBuffers[device] = new CudaDeviceCopyBuffer{device, ctx.size, ctx.num};
    }
    CopyResult result;
    result.Push(instance.DoCopy(srcBuffers, dstBuffers));
    for (size_t device = 0; device < ctx.nDevice; device++) {
        delete srcBuffers[device];
        delete dstBuffers[device];
    }
    result.Show("[[ " + Key() + " ]] " + Brief());
}

DEFINE_COPY_CASE(Device2DeviceCECase, "device_to_device_ce",
                 "memcpy from device to device with ce one by one", ctx)
{
    CudaMemcpyDevice2DeviceCopyInitiator initiator;
    CopyInstance instance{&initiator, ctx.iter, false};
    CopyResult result;
    for (size_t device = 0; device < ctx.nDevice; device++) {
        CudaDeviceCopyBuffer srcBuffer{device, ctx.size, ctx.num};
        CudaDeviceCopyBuffer dstBuffer{device, ctx.size, ctx.num};
        result.Push(instance.DoCopy(&srcBuffer, &dstBuffer));
    }
    result.Show("[[ " + Key() + " ]] " + Brief());
}

DEFINE_COPY_CASE(OneDevice2AllDeviceCECase, "one_device_to_all_device_ce",
                 "memcpy from one device to all device with ce", ctx)
{
    CudaMemcpyDevice2DeviceCopyInitiator initiator;
    CopyInstance instance{&initiator, ctx.iter, false};
    CopyResult result;
    CudaDeviceCopyBuffer srcBuffer{0, ctx.size, ctx.num};
    for (size_t device = 0; device < ctx.nDevice; device++) {
        CudaDeviceCopyBuffer dstBuffer{device, ctx.size, ctx.num};
        result.Push(instance.DoCopy(&srcBuffer, &dstBuffer));
    }
    result.Show("[[ " + Key() + " ]] " + Brief());
}
