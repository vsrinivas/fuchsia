// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "root_resource.h"

#include <lib/zx/time.h>

#include "gtest/gtest.h"

class SystemMonitorRootResourceTest : public ::testing::Test {};

TEST_F(SystemMonitorRootResourceTest, GatherData) {
  zx_handle_t root_resource;
  zx_status_t status = harvester::GetRootResource(&root_resource);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_NE(root_resource, ZX_HANDLE_INVALID);

  // Arbitrary choice of system calls to try out the handle.
  zx_info_cpu_stats_t stats;
  size_t actual, avail;
  status = zx_object_get_info(root_resource, ZX_INFO_CPU_STATS, &stats,
                              sizeof(stats), &actual, &avail);
  EXPECT_EQ(status, ZX_OK);
  // This test is not about this data, so only a few sanity checks are
  // performed.
  EXPECT_EQ(actual, 1ULL);
  EXPECT_GT(avail, 0ULL);
  // Expecting less than 5,000 cores seems reasonable, for now.
  EXPECT_LT(actual, 5000ULL);
  EXPECT_GT(stats.idle_time, 0LL);
  // Assuming less than ten years of accumulated idle time is reasonable.
  const zx_duration_t TEN_YEARS = 315360000000000000ULL;
  EXPECT_LT(stats.idle_time, TEN_YEARS);
  EXPECT_GT(stats.syscalls, 0ULL);
}
