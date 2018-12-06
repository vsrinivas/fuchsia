// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/ordinals.h"

#include <optional>

#define BORINGSSL_NO_CXX
#include <openssl/sha.h>

namespace fidl {
namespace ordinals {

std::string GetSelector(const raw::AttributeList* attributes, SourceLocation name) {
    if (attributes != nullptr) {
        const size_t size = attributes->attributes.size();
        for (int i = 0; i < size; i++) {
            if (attributes->attributes[i]->name == "Selector") {
                return attributes->attributes[i]->value;
            }
        }
    }
    return std::string(name.data().data(), name.data().size());
}

raw::Ordinal GetOrdinal(const std::vector<StringView>& library_name,
                        const StringView& interface_name,
                        const raw::InterfaceMethod& method) {
    if (method.ordinal != nullptr)
        return *method.ordinal;

    std::string method_name = GetSelector(method.attributes.get(), method.identifier->location());
    std::string full_name;
    bool once = false;
    for (StringView id : library_name) {
        if (once) {
            full_name.push_back('.');
        } else {
            once = true;
        }
        full_name.append(id.data(), id.size());
    }
    full_name.append(".");
    full_name.append(interface_name.data(), interface_name.size());
    full_name.append("/");
    full_name.append(method_name);

    uint8_t digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const uint8_t*>(full_name.data()), full_name.size(), digest);
    // The following dance ensures that we treat the bytes as a little-endian
    // int32 regardless of host byte order.
    uint32_t ordinal = static_cast<uint32_t>(digest[0]) |
        static_cast<uint32_t>(digest[1]) << 8 |
        static_cast<uint32_t>(digest[2]) << 16 |
        static_cast<uint32_t>(digest[3]) << 24;

    ordinal &= 0x7fffffff;
    return raw::Ordinal(*method.identifier, ordinal);
}

} // namespace ordinals
} // namespace fidl
