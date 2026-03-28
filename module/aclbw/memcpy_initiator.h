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
#ifndef ACLBW_MEMCPY_INITIATOR_H
#define ACLBW_MEMCPY_INITIATOR_H

#include <cstring>
#include <vector>
#include "memory_buffer.h"

class MemcpyInitiator {
public:
    virtual ~MemcpyInitiator() = default;
    virtual void Copy(void* src, void* dst, size_t size, aclrtStream stream) const = 0;
    virtual void Copy(void** src, void** dst, size_t* size, size_t number, uint32_t deviceId,
                      aclrtStream stream) const
    {
        for (size_t i = 0; i < number; i++) { Copy(src[i], dst[i], size[i], stream); }
    }
    virtual void Copy(const MemoryBuffer& src, const MemoryBuffer& dst, aclrtStream stream) const
    {
        for (size_t i = 0; i < src.Number(); ++i) { Copy(src[i], dst[i], src.Size(), stream); }
    }
};

class Host2DeviceCEMemcpyInitiator : public MemcpyInitiator {
public:
    void Copy(void* src, void* dst, size_t size, aclrtStream stream) const override
    {
        ACLBW_ASCEND_ASSERT(
            aclrtMemcpyAsync(dst, size, src, size, ACL_MEMCPY_HOST_TO_DEVICE, stream));
    }
};

class Host2DeviceCEMemcpyBatchInitiator : public Host2DeviceCEMemcpyInitiator {
public:
    void Copy(void** src, void** dst, size_t* size, size_t number, uint32_t deviceId,
              aclrtStream stream) const override
    {
        aclrtMemcpyBatchAttr attr;
        memset(&attr, 0, sizeof(attr));
        attr.srcLoc.type = ACL_MEM_LOCATION_TYPE_HOST;
        attr.dstLoc.type = ACL_MEM_LOCATION_TYPE_DEVICE;
        attr.dstLoc.id = deviceId;
        std::vector<aclrtMemcpyBatchAttr> attrArray{attr};
        std::vector<size_t> attrIdxArray(number, 0);
        size_t failureIdx = 0;
        ACLBW_ASCEND_ASSERT(aclrtMemcpyBatchAsync(dst, size, src, size, number, attrArray.data(),
                                                  attrIdxArray.data(), attrArray.size(),
                                                  &failureIdx, stream));
    }
};

class Device2HostCEMemcpyInitiator : public MemcpyInitiator {
public:
    void Copy(void* src, void* dst, size_t size, aclrtStream stream) const override
    {
        ACLBW_ASCEND_ASSERT(
            aclrtMemcpyAsync(dst, size, src, size, ACL_MEMCPY_DEVICE_TO_HOST, stream));
    }
};

#endif  // ACLBW_MEMCPY_INITIATOR_H
