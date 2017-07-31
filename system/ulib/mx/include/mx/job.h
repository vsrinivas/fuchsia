// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/task.h>
#include <magenta/process.h>

namespace mx {

class job : public task<job> {
public:
    static constexpr mx_obj_type_t TYPE = MX_OBJ_TYPE_JOB;

    constexpr job() = default;

    explicit job(mx_handle_t value) : task(value) {}

    explicit job(handle&& h) : task(h.release()) {}

    job(job&& other) : task(other.release()) {}

    job& operator=(job&& other) {
        reset(other.release());
        return *this;
    }

    static mx_status_t create(mx_handle_t parent_job, uint32_t options, job* result);

    mx_status_t set_policy(uint32_t options, uint32_t topic, void* policy, uint32_t count) const {
      return mx_job_set_policy(get(), options, topic, policy, count);
    }

    // Ideally this would be called mx::job::default(), but default is a
    // C++ keyword and cannot be used as a function name.
    static inline const unowned<job> default_job() {
        return unowned<job>(mx_job_default());
    }
};

using unowned_job = const unowned<job>;

} // namespace mx
