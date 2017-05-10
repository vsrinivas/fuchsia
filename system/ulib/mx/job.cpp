// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mx/job.h>

#include <magenta/syscalls.h>

namespace mx {

mx_status_t job::create(mx_handle_t parent_job, uint32_t flags, job* result) {
    mx_handle_t h;
    mx_status_t status = mx_job_create(parent_job, flags, &h);
    if (status < 0) {
        result->reset(MX_HANDLE_INVALID);
    } else {
        result->reset(h);
    }
    return status;
}

mx_status_t job::set_policy(uint32_t options, uint32_t topic, void* policy, uint32_t count) {
    return mx_job_set_policy(get(), options, topic, policy, count);
}

} // namespace mx
