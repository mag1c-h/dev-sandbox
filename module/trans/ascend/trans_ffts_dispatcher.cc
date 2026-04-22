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
#include "trans_ffts_dispatcher.h"

class FftsDispatcher {
public:
    FftsDispatcher(std::size_t device, std::size_t nH2HThread, std::size_t nH2DThread) {}
    ~FftsDispatcher() {}
    void H2DAsync(void** src, void** dst, std::size_t size, std::size_t number) {}
    void Sync() {}
};

FftsDispatcherHandle TransMakeFftsDispatcher(std::size_t device, std::size_t nH2HThread,
                                             std::size_t nH2DThread)
{
    return static_cast<FftsDispatcherHandle>(new FftsDispatcher(device, nH2HThread, nH2DThread));
}

void TransReleaseFftsDispatcher(FftsDispatcherHandle handle)
{
    if (!handle) { return; }
    delete static_cast<FftsDispatcher*>(handle);
}

void TransSubmitH2DIo2FftsDispatcher(void** src, void** dst, std::size_t size, std::size_t number,
                                     FftsDispatcherHandle handle)
{
    static_cast<FftsDispatcher*>(handle)->H2DAsync(src, dst, size, number);
}

void TransSyncFftsDispatcher(FftsDispatcherHandle handle)
{
    static_cast<FftsDispatcher*>(handle)->Sync();
}
