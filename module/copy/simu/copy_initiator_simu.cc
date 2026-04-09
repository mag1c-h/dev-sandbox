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
#include <cstring>
#include "copy_initiator.h"

std::string H2DCopyInitiator::Name() const { return "memcpy"; }

void H2DCopyInitiator::Copy(void* src, void* dst, size_t size, void* args) const
{
    std::memcpy(dst, src, size);
}

void H2DCopyInitiator::Copy(void* const* src, void* const* dst, size_t size, size_t number,
                            void* args) const
{
    for (size_t i = 0; i < number; ++i) { std::memcpy(dst[i], src[i], size); }
}

std::string D2HCopyInitiator::Name() const { return "memcpy"; }

void D2HCopyInitiator::Copy(void* src, void* dst, size_t size, void* args) const
{
    std::memcpy(dst, src, size);
}

void D2HCopyInitiator::Copy(void* const* src, void* const* dst, size_t size, size_t number,
                            void* args) const
{
    for (size_t i = 0; i < number; ++i) { std::memcpy(dst[i], src[i], size); }
}

std::string D2DCopyInitiator::Name() const { return "memcpy"; }

void D2DCopyInitiator::Copy(void* src, void* dst, size_t size, void* args) const
{
    std::memcpy(dst, src, size);
}

void D2DCopyInitiator::Copy(void* const* src, void* const* dst, size_t size, size_t number,
                            void* args) const
{
    for (size_t i = 0; i < number; ++i) { std::memcpy(dst[i], src[i], size); }
}

H2DBatchCopyInitiator::H2DBatchCopyInitiator(size_t device, size_t number) {}

H2DBatchCopyInitiator::~H2DBatchCopyInitiator() {}

std::string H2DBatchCopyInitiator::Name() const { return "memcpy"; }

void H2DBatchCopyInitiator::Copy(void* src, void* dst, size_t size, void* args) const
{
    std::memcpy(dst, src, size);
}

void H2DBatchCopyInitiator::Copy(void* const* src, void* const* dst, size_t size, size_t number,
                                 void* args) const
{
    for (size_t i = 0; i < number; ++i) { std::memcpy(dst[i], src[i], size); }
}

D2HBatchCopyInitiator::D2HBatchCopyInitiator(size_t device, size_t number) {}

D2HBatchCopyInitiator::~D2HBatchCopyInitiator() {}

std::string D2HBatchCopyInitiator::Name() const { return "memcpy"; }

void D2HBatchCopyInitiator::Copy(void* src, void* dst, size_t size, void* args) const
{
    std::memcpy(dst, src, size);
}

void D2HBatchCopyInitiator::Copy(void* const* src, void* const* dst, size_t size, size_t number,
                                 void* args) const
{
    for (size_t i = 0; i < number; ++i) { std::memcpy(dst[i], src[i], size); }
}
