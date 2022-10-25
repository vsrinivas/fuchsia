// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/time.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <gtest/gtest.h>

#include "src/ui/scenic/lib/scenic/util/scheduler_profile.h"

TEST(Util, GetSchedulerProfile) {
  {
    const zx_status_t status = util::SetSchedulerRole(zx::thread::self(), "fuchsia.test-role:ok");
    EXPECT_EQ(ZX_OK, status);
  }
  {
    const zx_status_t status =
        util::SetSchedulerRole(zx::thread::self(), "fuchsia.test-role:not-found");
    EXPECT_EQ(ZX_ERR_NOT_FOUND, status);
  }
}
