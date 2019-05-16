// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <crashsvc/crashsvc.h>

#include <threads.h>

#include <lib/zx/job.h>
#include <zxtest/zxtest.h>

namespace {

TEST(crashsvc, StartAndStop) {
    zx::job job;
    ASSERT_OK(zx::job::create(*zx::job::default_job(), 0, &job));

    thrd_t thread;
    zx::job job_copy;
    ASSERT_OK(job.duplicate(ZX_RIGHT_SAME_RIGHTS, &job_copy));
    ASSERT_OK(start_crashsvc(std::move(job_copy), ZX_HANDLE_INVALID, &thread));

    ASSERT_OK(job.kill());

    int exit_code = -1;
    EXPECT_EQ(thrd_join(thread, &exit_code), thrd_success);
    EXPECT_EQ(exit_code, 0);
}

} // namespace
