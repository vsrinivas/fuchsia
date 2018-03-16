// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/job.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t job::create(zx_handle_t parent_job, uint32_t flags, job* result) {
    zx_handle_t h;
    zx_status_t status = zx_job_create(parent_job, flags, &h);
    if (status < 0) {
        result->reset(ZX_HANDLE_INVALID);
    } else {
        result->reset(h);
    }
    return status;
}

} // namespace zx
