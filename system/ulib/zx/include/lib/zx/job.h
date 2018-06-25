// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/task.h>
#include <zircon/process.h>

namespace zx {

class job : public task<job> {
public:
    static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_JOB;

    constexpr job() = default;

    explicit job(zx_handle_t value) : task(value) {}

    explicit job(handle&& h) : task(h.release()) {}

    job(job&& other) : task(other.release()) {}

    job& operator=(job&& other) {
        reset(other.release());
        return *this;
    }

    static zx_status_t create(zx_handle_t parent_job, uint32_t options, job* result);

    zx_status_t set_policy(uint32_t options, uint32_t topic, void* policy, uint32_t count) const {
      return zx_job_set_policy(get(), options, topic, policy, count);
    }

    // Ideally this would be called zx::job::default(), but default is a
    // C++ keyword and cannot be used as a function name.
    static inline const legacy_unowned<job> default_job() {
        return legacy_unowned<job>(zx_job_default());
    }
};

using unowned_job = unowned<job>;

} // namespace zx
