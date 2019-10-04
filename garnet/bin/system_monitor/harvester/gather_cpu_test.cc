// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_cpu.h"

#include "dockyard_proxy_fake.h"
#include "gtest/gtest.h"
#include "root_resource.h"

class GatherCpuTest : public ::testing::Test {};

TEST_F(GatherCpuTest, CheckValues) {
  zx_handle_t root_resource;
  zx_status_t ret = harvester::GetRootResource(&root_resource);
  ASSERT_EQ(ret, ZX_OK);
  harvester::DockyardProxyFake dockyard_proxy;

  harvester::GatherCpu gatherer(root_resource, &dockyard_proxy);
  uint64_t test_value;

  gatherer.GatherDeviceProperties();
  EXPECT_TRUE(dockyard_proxy.CheckValueSent("cpu:count", &test_value));
  EXPECT_LT(1ULL, test_value);

  gatherer.Gather();
  EXPECT_TRUE(dockyard_proxy.CheckValueSent("cpu:0:busy_time", &test_value));
  EXPECT_LT(1000ULL, test_value);  // Test value is arbitrary.
  const uint64_t NSEC_YEAR = 31536000000000000ULL;
  EXPECT_GT(10 * NSEC_YEAR, test_value);  // Test value is arbitrary.
}
