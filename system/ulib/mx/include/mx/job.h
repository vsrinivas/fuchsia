// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/handle.h>

namespace mx {

class job : public handle<job> {
public:
    job() = default;

    explicit job(handle<void>&& h) : handle(h.release()) {}

    job(job&& other) : handle(other.release()) {}

    job& operator=(job&& other) {
        reset(other.release());
        return *this;
    }

    static mx_status_t create(const job& parent, uint32_t options, job* result);
};

} // namespace mx
