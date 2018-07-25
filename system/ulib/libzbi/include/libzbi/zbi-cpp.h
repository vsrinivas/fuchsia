// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This Header is a thin C++ wrapper around the C ZBI processing API provided in
// ulib/zbi/*
//
// Documentation for methods can also be found in ulib/zbi/include/zbi/zbi.h

#pragma once

#include "zbi.h"

#include <stddef.h>
#include <zircon/boot/image.h>

namespace zbi {

class Zbi {
public:
    explicit Zbi(uint8_t* base) : base_(base) {
        zbi_header_t* hdr = reinterpret_cast<zbi_header_t*>(base_);
        capacity_ = hdr->length + sizeof(*hdr);
    }

    Zbi(uint8_t* base, size_t capacity)
        : base_(base)
        , capacity_(capacity) {}

    zbi_result_t Check(zbi_header_t** err) const {
        return zbi_check(base_, err);
    }

    zbi_result_t CheckComplete(zbi_header_t** err = nullptr) const {
        return zbi_check_complete(base_, err);
    }

    zbi_result_t ForEach(zbi_foreach_cb_t cb, void* cookie) const {
        return zbi_for_each(base_, cb, cookie);
    }

    zbi_result_t AppendSection(uint32_t length, uint32_t type, uint32_t extra,
                               uint32_t flags, const void* payload) {
        return zbi_append_section(base_, capacity_, length, type, extra, flags,
                                  payload);
    }

    zbi_result_t CreateSection(uint32_t length, uint32_t type, uint32_t extra,
                               uint32_t flags, void** payload) {
        return zbi_create_section(base_, capacity_, length, type, extra, flags,
                                  payload);
    }

    const uint8_t* Base() const { return base_; };
    uint32_t Length() const {
        return Header()->length + static_cast<uint32_t>(sizeof(zbi_header_t));
    }

protected:
    uint8_t* base_ = nullptr;
    size_t capacity_ = 0;

    Zbi() = default;

    zbi_header_t* Header() {
        return reinterpret_cast<zbi_header_t*>(base_);
    }
    const zbi_header_t* Header() const {
        return reinterpret_cast<const zbi_header_t*>(base_);
    }
    void* Payload() {
        return reinterpret_cast<void*>(Header() + 1);
    }
};

} // namespace zbi
