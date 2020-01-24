// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/time.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <gtest/gtest.h>

#include "src/ui/scenic/lib/scenic/util/scheduler_profile.h"

TEST(Util, GetSchedulerProfile) {
  const auto capacity = zx::msec(5);
  const auto deadline = zx::msec(10);
  const auto period = deadline;

  const auto profile = util::GetSchedulerProfile(capacity, deadline, period);
  EXPECT_TRUE(profile.is_valid());

  zx_info_handle_basic_t info{};
  const auto status = profile.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  EXPECT_EQ(ZX_OK, status);
  EXPECT_EQ(ZX_OBJ_TYPE_PROFILE, info.type);
}
