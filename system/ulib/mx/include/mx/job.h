// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/task.h>

namespace mx {

class job : public task<job> {
public:
    job() = default;

    explicit job(handle<void>&& h) : task(h.release()) {}
    job(job&& other) : task(other.release()) {}

    job& operator=(job&& other) {
        reset(other.release());
        return *this;
    }

    static mx_status_t create(mx_handle_t parent_job, uint32_t options,
                              job* result);
};

} // namespace mx
