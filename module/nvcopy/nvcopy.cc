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

int main(int argc, char const* argv[])
{
    std::vector<std::shared_ptr<CopyCase>> cases = {
        std::make_shared<Host2DeviceCECopyCase<4 * 1024, 2048, 1024>>("H2D4KIoCE"),
        std::make_shared<Host2DeviceCECopyCase<16 * 1024, 2048, 1024>>("H2D16KIoCE"),
        std::make_shared<Host2DeviceCECopyCase<32 * 1024, 2048, 512>>("H2D32KIoCE"),
        std::make_shared<Host2DeviceCECopyCase<64 * 1024, 2048, 256>>("H2D64KIoCE"),
        std::make_shared<Host2DeviceCECopyCase<128 * 1024, 2048, 128>>("H2D128KIoCE"),
        std::make_shared<Host2DeviceCECopyCase<1024 * 1024, 1024, 32>>("H2D1MIoCE"),
        std::make_shared<Host2DeviceCECopyCase<8 * 1024 * 1024, 512, 16>>("H2D8MIoCE"),
        std::make_shared<Host2DeviceCECopyCase<512 * 1024 * 1024, 8, 8>>("H2D512MIoCE"),
        std::make_shared<OneHost2AllDeviceCECopyCase<4 * 1024, 2048, 1024>>("H2D4KIoCE"),
        std::make_shared<OneHost2AllDeviceCECopyCase<16 * 1024, 2048, 1024>>("H2D16KIoCE"),
        std::make_shared<OneHost2AllDeviceCECopyCase<32 * 1024, 2048, 512>>("H2D32KIoCE"),
        std::make_shared<OneHost2AllDeviceCECopyCase<64 * 1024, 2048, 256>>("H2D64KIoCE"),
        std::make_shared<OneHost2AllDeviceCECopyCase<128 * 1024, 2048, 128>>("H2D128KIoCE"),
        std::make_shared<OneHost2AllDeviceCECopyCase<1024 * 1024, 1024, 32>>("H2D1MIoCE"),
        std::make_shared<OneHost2AllDeviceCECopyCase<8 * 1024 * 1024, 512, 16>>("H2D8MIoCE"),
        std::make_shared<OneHost2AllDeviceCECopyCase<512 * 1024 * 1024, 8, 8>>("H2D512MIoCE"),
    };

    for (auto test : cases) { test->Run(); }
    return 0;
}
