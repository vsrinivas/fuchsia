// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/handle.h>

namespace mx {

class eventpair : public handle<eventpair> {
public:
    eventpair() = default;

    explicit eventpair(handle<void>&& h) : handle(h.release()) {}

    eventpair(eventpair&& other) : handle(other.release()) {}

    eventpair& operator=(eventpair&& other) {
        reset(other.release());
        return *this;
    }

    static mx_status_t create(uint32_t options, eventpair* endpoint0, eventpair* endpoint1);
};

} // namespace mx
