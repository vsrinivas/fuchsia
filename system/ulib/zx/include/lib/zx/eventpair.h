// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/handle.h>
#include <lib/zx/object.h>

namespace zx {

class eventpair : public object<eventpair> {
public:
    static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_EVENT_PAIR;

    constexpr eventpair() = default;

    explicit eventpair(zx_handle_t value) : object(value) {}

    explicit eventpair(handle&& h) : object(h.release()) {}

    eventpair(eventpair&& other) : object(other.release()) {}

    eventpair& operator=(eventpair&& other) {
        reset(other.release());
        return *this;
    }

    static zx_status_t create(uint32_t options, eventpair* endpoint0,
                              eventpair* endpoint1);
};

using unowned_eventpair = const unowned<eventpair>;

} // namespace zx
