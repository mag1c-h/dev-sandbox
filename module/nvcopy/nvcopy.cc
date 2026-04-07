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
#include <memory>
#include "copy_case.h"

#define KB *1024
#define MB *(1024 KB)

int main(int argc, char const* argv[])
{
    constexpr size_t nDevice = 8;
    std::vector<std::shared_ptr<CopyCase>> cases = {
        std::make_shared<Host2DeviceCECopyCase<4 KB, 2048, 1024>>("H2D4KIoCE", nDevice),
        std::make_shared<Host2DeviceCECopyCase<16 KB, 2048, 1024>>("H2D16KIoCE", nDevice),
        std::make_shared<Host2DeviceCECopyCase<32 KB, 2048, 512>>("H2D32KIoCE", nDevice),
        std::make_shared<Host2DeviceCECopyCase<64 KB, 2048, 256>>("H2D64KIoCE", nDevice),
        std::make_shared<Host2DeviceCECopyCase<128 KB, 2048, 128>>("H2D128KIoCE", nDevice),
        std::make_shared<Host2DeviceCECopyCase<1 MB, 1024, 32>>("H2D1MIoCE", nDevice),
        std::make_shared<Host2DeviceCECopyCase<8 MB, 512, 16>>("H2D8MIoCE", nDevice),
        std::make_shared<Host2DeviceCECopyCase<512 MB, 8, 8>>("H2D512MIoCE", nDevice),
        std::make_shared<Host2DeviceSMCopyCase<4 KB, 2048, 1024>>("H2D4KIoSM", nDevice),
        std::make_shared<Host2DeviceSMCopyCase<16 KB, 2048, 1024>>("H2D16KIoSM", nDevice),
        std::make_shared<Host2DeviceSMCopyCase<32 KB, 2048, 512>>("H2D32KIoSM", nDevice),
        std::make_shared<Host2DeviceSMCopyCase<64 KB, 2048, 256>>("H2D64KIoSM", nDevice),
        std::make_shared<Host2DeviceSMCopyCase<128 KB, 2048, 128>>("H2D128KIoSM", nDevice),
        std::make_shared<Host2DeviceSMCopyCase<1 MB, 1024, 32>>("H2D1MIoSM", nDevice),
        std::make_shared<Host2DeviceSMCopyCase<8 MB, 512, 16>>("H2D8MIoSM", nDevice),
        std::make_shared<Host2DeviceSMCopyCase<512 MB, 8, 8>>("H2D512MIoSM", nDevice),
        std::make_shared<OneHost2AllDeviceCECopyCase<4 KB, 2048, 1024>>("H2D4KIoCE", nDevice),
        std::make_shared<OneHost2AllDeviceCECopyCase<16 KB, 2048, 1024>>("H2D16KIoCE", nDevice),
        std::make_shared<OneHost2AllDeviceCECopyCase<32 KB, 2048, 512>>("H2D32KIoCE", nDevice),
        std::make_shared<OneHost2AllDeviceCECopyCase<64 KB, 2048, 256>>("H2D64KIoCE", nDevice),
        std::make_shared<OneHost2AllDeviceCECopyCase<128 KB, 2048, 128>>("H2D128KIoCE", nDevice),
        std::make_shared<OneHost2AllDeviceCECopyCase<1 MB, 1024, 32>>("H2D1MIoCE", nDevice),
        std::make_shared<OneHost2AllDeviceCECopyCase<8 MB, 512, 16>>("H2D8MIoCE", nDevice),
        std::make_shared<OneHost2AllDeviceCECopyCase<512 MB, 8, 8>>("H2D512MIoCE", nDevice),
        std::make_shared<AllHost2AllDeviceCECopyCase<4 KB, 2048, 1024>>("H2D4KIoCE", nDevice),
        std::make_shared<AllHost2AllDeviceCECopyCase<16 KB, 2048, 1024>>("H2D16KIoCE", nDevice),
        std::make_shared<AllHost2AllDeviceCECopyCase<32 KB, 2048, 512>>("H2D32KIoCE", nDevice),
        std::make_shared<AllHost2AllDeviceCECopyCase<64 KB, 2048, 256>>("H2D64KIoCE", nDevice),
        std::make_shared<AllHost2AllDeviceCECopyCase<128 KB, 2048, 128>>("H2D128KIoCE", nDevice),
        std::make_shared<AllHost2AllDeviceCECopyCase<1 MB, 1024, 32>>("H2D1MIoCE", nDevice),
        std::make_shared<AllHost2AllDeviceCECopyCase<8 MB, 512, 16>>("H2D8MIoCE", nDevice),
        std::make_shared<AllHost2AllDeviceCECopyCase<512 MB, 8, 8>>("H2D512MIoCE", nDevice),
        std::make_shared<Device2DeviceCECopyCase<4 KB, 2048, 1024>>("D2D4KIoCE", nDevice),
        std::make_shared<Device2DeviceCECopyCase<16 KB, 2048, 1024>>("D2D16KIoCE", nDevice),
        std::make_shared<Device2DeviceCECopyCase<32 KB, 2048, 512>>("D2D32KIoCE", nDevice),
        std::make_shared<Device2DeviceCECopyCase<64 KB, 2048, 256>>("D2D64KIoCE", nDevice),
        std::make_shared<Device2DeviceCECopyCase<128 KB, 2048, 128>>("D2D128KIoCE", nDevice),
        std::make_shared<Device2DeviceCECopyCase<1 MB, 1024, 32>>("D2D1MIoCE", nDevice),
        std::make_shared<Device2DeviceCECopyCase<8 MB, 512, 16>>("D2D8MIoCE", nDevice),
        std::make_shared<Device2DeviceCECopyCase<512 MB, 8, 8>>("D2D512MIoCE", nDevice),
        std::make_shared<OneDevice2AllDeviceCECopyCase<4 KB, 2048, 1024>>("D2D4KIoCE", nDevice),
        std::make_shared<OneDevice2AllDeviceCECopyCase<16 KB, 2048, 1024>>("D2D16KIoCE", nDevice),
        std::make_shared<OneDevice2AllDeviceCECopyCase<32 KB, 2048, 512>>("D2D32KIoCE", nDevice),
        std::make_shared<OneDevice2AllDeviceCECopyCase<64 KB, 2048, 256>>("D2D64KIoCE", nDevice),
        std::make_shared<OneDevice2AllDeviceCECopyCase<128 KB, 2048, 128>>("D2D128KIoCE", nDevice),
        std::make_shared<OneDevice2AllDeviceCECopyCase<1 MB, 1024, 32>>("D2D1MIoCE", nDevice),
        std::make_shared<OneDevice2AllDeviceCECopyCase<8 MB, 512, 16>>("D2D8MIoCE", nDevice),
        std::make_shared<OneDevice2AllDeviceCECopyCase<512 MB, 8, 8>>("D2D512MIoCE", nDevice),
    };

    for (auto test : cases) { test->Run(); }
    return 0;
}
