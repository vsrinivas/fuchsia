// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/task.h>

namespace mx {

class job : public task<job> {
public:
    static constexpr mx_obj_type_t TYPE = MX_OBJ_TYPE_JOB;

    job() = default;

    explicit job(handle&& h) : task(h.release()) {}
    job(job&& other) : task(other.release()) {}

    job& operator=(job&& other) {
        reset(other.release());
        return *this;
    }

    static mx_status_t create(mx_handle_t parent_job, uint32_t options, job* result);

    mx_status_t set_policy(uint32_t options, uint32_t topic, void* policy, uint32_t count);
};

} // namespace mx
