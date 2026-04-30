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

#pragma once

#include "registry.h"

namespace ucm::transfer {

class Factory {
public:
    static Expected<std::unique_ptr<IStream>> create(const AnyAddress& src, const AnyAddress& dst)
    {
        auto creator = Registry::instance().get_creator(src.type_name(), dst.type_name(),
                                                        DefaultProtocol::name());
        if (!creator) {
            return Error{
                ErrorCode::UnsupportedTransfer,
                "No transfer registered for " + src.type_name() + " -> " + dst.type_name()};
        }

        AnyProtocol protocol(DefaultProtocol{});
        auto stream = creator(src, dst, protocol);
        if (!stream) {
            return Error{ErrorCode::ResourceAllocationFailed, "Failed to create transfer stream"};
        }

        return stream;
    }
    template <typename Protocol>
    static Expected<std::unique_ptr<IStream>> create(const AnyAddress& src, const AnyAddress& dst,
                                                     Protocol protocol)
    {
        auto creator =
            Registry::instance().get_creator(src.type_name(), dst.type_name(), Protocol::name());
        if (!creator) {
            return Error{ErrorCode::UnsupportedProtocol,
                         "Protocol '" + Protocol::name() + "' not supported for " +
                             src.type_name() + " -> " + dst.type_name()};
        }

        AnyProtocol proto_wrapper(protocol);
        auto stream = creator(src, dst, proto_wrapper);
        if (!stream) {
            return Error{ErrorCode::ResourceAllocationFailed, "Failed to create transfer stream"};
        }

        return stream;
    }
    static std::vector<std::string> list_protocols(const std::string& src_type,
                                                   const std::string& dst_type)
    {
        return Registry::instance().list_protocols(src_type, dst_type);
    }
    static bool is_supported(const std::string& src_type, const std::string& dst_type)
    {
        return Registry::instance().has_creator(src_type, dst_type, DefaultProtocol::name());
    }
    template <typename Protocol>
    static bool is_supported(const std::string& src_type, const std::string& dst_type)
    {
        return Registry::instance().has_creator(src_type, dst_type, Protocol::name());
    }
    static std::vector<std::pair<std::string, std::string>> list_all_transfers()
    {
        return Registry::instance().list_all_transfers();
    }
};

}  // namespace ucm::transfer
