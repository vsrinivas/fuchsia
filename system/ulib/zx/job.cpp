// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/job.h>

#include <zircon/syscalls.h>

namespace zx {

zx_status_t job::create(const job& parent, uint32_t flags, job* result) {
    // Allow for aliasing of the same container to |result| and |parent|.
    job h;
    zx_status_t status =
        zx_job_create(parent.get(), flags, h.reset_and_get_address());
    result->reset(h.release());
    return status;
}

} // namespace zx
