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

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "protocol.h"
#include "stream.h"

namespace ucm::transfer {

class Registry {
public:
    using Creator = std::function<std::unique_ptr<IStream>(const AnyAddress&, const AnyAddress&,
                                                           const AnyProtocol&)>;

    static Registry& instance()
    {
        static Registry registry;
        return registry;
    }

    void register_creator(const std::string& src_type, const std::string& dst_type,
                          const std::string& protocol_name, Creator creator)
    {
        std::string key = make_key(src_type, dst_type, protocol_name);
        creators_[key] = std::move(creator);
    }
    Creator get_creator(const std::string& src_type, const std::string& dst_type,
                        const std::string& protocol_name) const
    {
        std::string key = make_key(src_type, dst_type, protocol_name);
        auto it = creators_.find(key);
        if (it != creators_.end()) { return it->second; }
        return nullptr;
    }
    bool has_creator(const std::string& src_type, const std::string& dst_type,
                     const std::string& protocol_name) const
    {
        std::string key = make_key(src_type, dst_type, protocol_name);
        return creators_.find(key) != creators_.end();
    }
    std::vector<std::string> list_protocols(const std::string& src_type,
                                            const std::string& dst_type) const
    {
        std::vector<std::string> protocols;
        std::string prefix = src_type + "->" + dst_type + ":";
        for (const auto& [key, creator] : creators_) {
            if (key.substr(0, prefix.size()) == prefix) {
                protocols.push_back(key.substr(prefix.size()));
            }
        }
        return protocols;
    }
    std::vector<std::pair<std::string, std::string>> list_all_transfers() const
    {
        std::vector<std::pair<std::string, std::string>> transfers;
        for (const auto& [key, creator] : creators_) {
            auto parts = split_key(key);
            transfers.emplace_back(parts.first, parts.second);
        }
        return transfers;
    }

private:
    Registry() = default;
    static std::string make_key(const std::string& src_type, const std::string& dst_type,
                                const std::string& protocol_name)
    {
        return src_type + "->" + dst_type + ":" + protocol_name;
    }
    static std::pair<std::string, std::string> split_key(const std::string& key)
    {
        auto arrow_pos = key.find("->");
        auto colon_pos = key.find(":");
        if (arrow_pos == std::string::npos || colon_pos == std::string::npos) { return {"", ""}; }
        return {key.substr(0, arrow_pos), key.substr(arrow_pos + 2, colon_pos - arrow_pos - 2)};
    }
    std::unordered_map<std::string, Creator> creators_;
};

}  // namespace ucm::transfer

#define REGISTER_TRANSFER_STREAM_WITH_PROTOCOL(Impl, SrcAddr, DstAddr, Protocol)               \
    __attribute__((constructor)) static void _init_##Impl##_##Protocol()                       \
    {                                                                                          \
        ::ucm::transfer::Registry::instance().register_creator(                                \
            SrcAddr::type_name(), DstAddr::type_name(), Protocol::name(),                      \
            [](const ::ucm::transfer::AnyAddress& src, const ::ucm::transfer::AnyAddress& dst, \
               const ::ucm::transfer::AnyProtocol& proto)                                      \
                -> std::unique_ptr<::ucm::transfer::IStream> {                                 \
                return std::make_unique<Impl>(src.cast<SrcAddr>(), dst.cast<DstAddr>(),        \
                                              proto.cast<Protocol>());                         \
            });                                                                                \
    }

#define REGISTER_TRANSFER_STREAM(Impl, SrcAddr, DstAddr)                                          \
    __attribute__((constructor)) static void _init_##Impl()                                       \
    {                                                                                             \
        ::ucm::transfer::Registry::instance().register_creator(                                   \
            SrcAddr::type_name(), DstAddr::type_name(), ::ucm::transfer::DefaultProtocol::name(), \
            [](const ::ucm::transfer::AnyAddress& src, const ::ucm::transfer::AnyAddress& dst,    \
               const ::ucm::transfer::AnyProtocol& proto)                                         \
                -> std::unique_ptr<::ucm::transfer::IStream> {                                    \
                return std::make_unique<Impl>(src.cast<SrcAddr>(), dst.cast<DstAddr>(),           \
                                              proto.cast<::ucm::transfer::DefaultProtocol>());    \
            });                                                                                   \
    }
