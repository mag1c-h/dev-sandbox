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
#ifndef COPY_INITIATOR_ASCEND_H
#define COPY_INITIATOR_ASCEND_H

#include <cstring>
#include <vector>
#include "copy_initiator.h"
#include "error_handle_ascend.h"

class H2DCopyInitiator : public CopyInitiator {
public:
    std::string Name() const override { return "CE"; }
    void Copy(void* const* src, void* const* dst, size_t size, size_t number,
              void* args) const override
    {
        auto stream = static_cast<aclrtStream>(args);
        for (size_t i = 0; i < number; ++i) {
            ASCEND_ASSERT(
                aclrtMemcpyAsync(dst[i], size, src[i], size, ACL_MEMCPY_HOST_TO_DEVICE, stream));
        }
    }
};

class D2DCopyInitiator : public CopyInitiator {
public:
    std::string Name() const override { return "CE"; }
    void Copy(void* const* src, void* const* dst, size_t size, size_t number,
              void* args) const override
    {
        auto stream = static_cast<aclrtStream>(args);
        for (size_t i = 0; i < number; ++i) {
            ASCEND_ASSERT(
                aclrtMemcpyAsync(dst[i], size, src[i], size, ACL_MEMCPY_DEVICE_TO_DEVICE, stream));
        }
    }
};

class H2DBatchCopyInitiator : public CopyInitiator {
    size_t device_;

public:
    H2DBatchCopyInitiator(size_t device) : CopyInitiator{}, device_{device} {}
    std::string Name() const override { return "BatchCE"; }
    void Copy(void* const* src, void* const* dst, size_t size, size_t number,
              void* args) const override
    {
        auto stream = static_cast<aclrtStream>(args);
        aclrtMemcpyBatchAttr attr;
        std::memset(&attr, 0, sizeof(attr));
        attr.srcLoc.type = ACL_MEM_LOCATION_TYPE_HOST;
        attr.dstLoc.type = ACL_MEM_LOCATION_TYPE_DEVICE;
        attr.dstLoc.id = device_;
        std::vector<aclrtMemcpyBatchAttr> attrArray{attr};
        std::vector<size_t> attrIdxArray(number, 0);
        std::vector<size_t> sizeArray(number, size);
        size_t failureIdx = 0;
        ASCEND_ASSERT(aclrtMemcpyBatchAsync(
            const_cast<void**>(dst), sizeArray.data(), const_cast<void**>(src), sizeArray.data(),
            number, attrArray.data(), attrIdxArray.data(), attrArray.size(), &failureIdx, stream));
    }
};

#endif  // COPY_INITIATOR_ASCEND_H
