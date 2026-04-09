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
#include "copy_initiator.h"
#include "error_handle_ascend.h"

std::string H2DCopyInitiator::Name() const { return "CE"; }

void H2DCopyInitiator::Copy(void* const* src, void* const* dst, size_t size, size_t number,
                            void* args) const
{
    auto stream = static_cast<aclrtStream>(args);
    for (size_t i = 0; i < number; ++i) {
        ASCEND_ASSERT(
            aclrtMemcpyAsync(dst[i], size, src[i], size, ACL_MEMCPY_HOST_TO_DEVICE, stream));
    }
}

std::string D2HCopyInitiator::Name() const { return "CE"; }

void D2HCopyInitiator::Copy(void* const* src, void* const* dst, size_t size, size_t number,
                            void* args) const
{
    auto stream = static_cast<aclrtStream>(args);
    for (size_t i = 0; i < number; ++i) {
        ASCEND_ASSERT(
            aclrtMemcpyAsync(dst[i], size, src[i], size, ACL_MEMCPY_DEVICE_TO_HOST, stream));
    }
}

std::string D2DCopyInitiator::Name() const { return "CE"; }

void D2DCopyInitiator::Copy(void* const* src, void* const* dst, size_t size, size_t number,
                            void* args) const
{
    auto stream = static_cast<aclrtStream>(args);
    for (size_t i = 0; i < number; ++i) {
        ASCEND_ASSERT(
            aclrtMemcpyAsync(dst[i], size, src[i], size, ACL_MEMCPY_DEVICE_TO_DEVICE, stream));
    }
}

SMCopyInitiator::SMCopyInitiator(size_t device, size_t number)
{
    const auto ptrSize = sizeof(void*) * number;
    ASCEND_ASSERT(aclrtSetDevice(device));
    ASCEND_ASSERT(aclrtMalloc(&dSrc_, ptrSize, ACL_MEM_MALLOC_HUGE_FIRST));
    ASCEND_ASSERT(aclrtMalloc(&dDst_, ptrSize, ACL_MEM_MALLOC_HUGE_FIRST));
}

SMCopyInitiator::~SMCopyInitiator()
{
    if (dSrc_) { ASCEND_ASSERT(aclrtFree(dSrc_)); }
    if (dDst_) { ASCEND_ASSERT(aclrtFree(dDst_)); }
}

std::string SMCopyInitiator::Name() const { return "SM"; }

void SMCopyInitiator::Copy(void* const* src, void* const* dst, size_t size, size_t number,
                           void* args) const
{
    ASSERT(false && "todo: implement this method");
}
