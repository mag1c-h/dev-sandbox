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
#include "copy_buffer_cuda.h"
#include "copy_case.h"
#include "copy_initiator_cuda.h"
#include "copy_instance_cuda.h"

DEFINE_COPY_CASE(Host2DeviceCECase, "host_to_device_ce",
                 "memcpy from host to device with ce one by one", ctx)
{
    H2DCopyInitiator initiator;
    CudaCopyInstance instance{&initiator, ctx.iter, false};
    CopyResult result;
    for (size_t device = 0; device < ctx.nDevice; device++) {
        HostCopyBuffer srcBuffer{device, ctx.size, ctx.num};
        DeviceCopyBuffer dstBuffer{device, ctx.size, ctx.num};
        result.Push(instance.DoCopy(&srcBuffer, &dstBuffer));
    }
    result.Show("[[ " + Key() + " ]] " + Brief());
}

DEFINE_COPY_CASE(Host2DeviceBatchCECase, "host_to_device_batch_ce",
                 "memcpy from host to device with batch ce one by one", ctx)
{
    CopyResult result;
    for (size_t device = 0; device < ctx.nDevice; device++) {
        HostCopyBuffer srcBuffer{device, ctx.size, ctx.num};
        DeviceCopyBuffer dstBuffer{device, ctx.size, ctx.num};
        H2DBatchCopyInitiator initiator{device};
        CudaCopyInstance instance{&initiator, ctx.iter, false};
        result.Push(instance.DoCopy(&srcBuffer, &dstBuffer));
    }
    result.Show("[[ " + Key() + " ]] " + Brief());
}

DEFINE_COPY_CASE(Host2DeviceSMCase, "host_to_device_sm",
                 "memcpy from host to device with sm one by one", ctx)
{
    CopyResult result;
    for (size_t device = 0; device < ctx.nDevice; device++) {
        HostCopyBuffer srcBuffer{device, ctx.size, ctx.num};
        DeviceCopyBuffer dstBuffer{device, ctx.size, ctx.num};
        SMCopyInitiator initiator(device, ctx.num);
        CudaCopyInstance instance{&initiator, ctx.iter, false};
        result.Push(instance.DoCopy(&srcBuffer, &dstBuffer));
    }
    result.Show("[[ " + Key() + " ]] " + Brief());
}

DEFINE_COPY_CASE(OneHost2AllDeviceCECase, "one_host_to_all_device_ce",
                 "memcpy from one host to all device with ce", ctx)
{
    H2DCopyInitiator initiator;
    CudaCopyInstance instance{&initiator, ctx.iter, false};
    CopyResult result;
    HostCopyBuffer srcBuffer{0, ctx.size, ctx.num};
    for (size_t device = 0; device < ctx.nDevice; device++) {
        DeviceCopyBuffer dstBuffer{device, ctx.size, ctx.num};
        result.Push(instance.DoCopy(&srcBuffer, &dstBuffer));
    }
    result.Show("[[ " + Key() + " ]] " + Brief());
}

DEFINE_COPY_CASE(OneHost2AllDeviceSMCase, "one_host_to_all_device_sm",
                 "memcpy from one host to all device with sm", ctx)
{
    CopyResult result;
    HostCopyBuffer srcBuffer{0, ctx.size, ctx.num};
    for (size_t device = 0; device < ctx.nDevice; device++) {
        DeviceCopyBuffer dstBuffer{device, ctx.size, ctx.num};
        SMCopyInitiator initiator{device, ctx.num};
        CudaCopyInstance instance{&initiator, ctx.iter, false};
        result.Push(instance.DoCopy(&srcBuffer, &dstBuffer));
    }
    result.Show("[[ " + Key() + " ]] " + Brief());
}

DEFINE_COPY_CASE(AllHost2AllDeviceCECase, "all_host_to_all_device_ce",
                 "memcpy from all host to all device with ce at one time", ctx)
{
    H2DCopyInitiator initiator;
    CudaCopyInstance instance{&initiator, ctx.iter, false};
    std::vector<const CopyBuffer*> srcBuffers(ctx.nDevice);
    std::vector<const CopyBuffer*> dstBuffers(ctx.nDevice);
    for (size_t device = 0; device < ctx.nDevice; device++) {
        srcBuffers[device] = new HostCopyBuffer{device, ctx.size, ctx.num};
        dstBuffers[device] = new DeviceCopyBuffer{device, ctx.size, ctx.num};
    }
    CopyResult result;
    result.Push(instance.DoCopyBatch(srcBuffers, dstBuffers));
    for (size_t device = 0; device < ctx.nDevice; device++) {
        delete srcBuffers[device];
        delete dstBuffers[device];
    }
    result.Show("[[ " + Key() + " ]] " + Brief());
}

DEFINE_COPY_CASE(Device2DeviceCECase, "device_to_device_ce",
                 "memcpy from device to device with ce one by one", ctx)
{
    D2DCopyInitiator initiator;
    CudaCopyInstance instance{&initiator, ctx.iter, false};
    CopyResult result;
    for (size_t device = 0; device < ctx.nDevice; device++) {
        DeviceCopyBuffer srcBuffer{device, ctx.size, ctx.num};
        DeviceCopyBuffer dstBuffer{device, ctx.size, ctx.num};
        result.Push(instance.DoCopy(&srcBuffer, &dstBuffer));
    }
    result.Show("[[ " + Key() + " ]] " + Brief());
}

DEFINE_COPY_CASE(OneDevice2AllDeviceCECase, "one_device_to_all_device_ce",
                 "memcpy from one device to all device with ce", ctx)
{
    D2DCopyInitiator initiator;
    CudaCopyInstance instance{&initiator, ctx.iter, false};
    CopyResult result;
    DeviceCopyBuffer srcBuffer{0, ctx.size, ctx.num};
    for (size_t device = 0; device < ctx.nDevice; device++) {
        DeviceCopyBuffer dstBuffer{device, ctx.size, ctx.num};
        result.Push(instance.DoCopy(&srcBuffer, &dstBuffer));
    }
    result.Show("[[ " + Key() + " ]] " + Brief());
}

DEFINE_COPY_CASE(Anonymous2DeviceCECase, "anonymous_to_device_ce",
                 "memcpy from anonymous to device one by one", ctx)
{
    H2DCopyInitiator initiator;
    CudaCopyInstance instance{&initiator, ctx.iter, false};
    CopyResult result;
    for (size_t device = 0; device < ctx.nDevice; device++) {
        AnonymousCopyBuffer srcBuffer{device, ctx.size, ctx.num};
        DeviceCopyBuffer dstBuffer{device, ctx.size, ctx.num};
        result.Push(instance.DoCopy(&srcBuffer, &dstBuffer));
    }
    result.Show("[[ " + Key() + " ]] " + Brief());
}

DEFINE_COPY_CASE(Anonymous2DeviceSMCase, "anonymous_to_device_sm",
                 "memcpy from anonymous to device with sm one by one", ctx)
{
    CopyResult result;
    for (size_t device = 0; device < ctx.nDevice; device++) {
        AnonymousCopyBuffer srcBuffer{device, ctx.size, ctx.num};
        DeviceCopyBuffer dstBuffer{device, ctx.size, ctx.num};
        SMCopyInitiator initiator(device, ctx.num);
        CudaCopyInstance instance{&initiator, ctx.iter, false};
        result.Push(instance.DoCopy(&srcBuffer, &dstBuffer));
    }
    result.Show("[[ " + Key() + " ]] " + Brief());
}
