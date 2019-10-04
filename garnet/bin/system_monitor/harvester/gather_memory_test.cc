// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_memory.h"

#include "dockyard_proxy_fake.h"
#include "gtest/gtest.h"
#include "root_resource.h"

class GatherMemoryTest : public ::testing::Test {};

TEST_F(GatherMemoryTest, Inspectable) {
  zx_handle_t root_resource;
  zx_status_t ret = harvester::GetRootResource(&root_resource);
  ASSERT_EQ(ret, ZX_OK);
  harvester::DockyardProxyFake dockyard_proxy;

  harvester::GatherMemory gatherer(root_resource, &dockyard_proxy);
  uint64_t test_value;

  gatherer.GatherDeviceProperties();
  // Test device_total_bytes.
  EXPECT_TRUE(
      dockyard_proxy.CheckValueSent("memory:device_total_bytes", &test_value));
  EXPECT_LT(1000ULL, test_value);  // Test value is arbitrary.
  const uint64_t TB = 1024ULL * 1024 * 1024 * 1024;
  EXPECT_GT(TB, test_value);  // Test value is arbitrary.

  gatherer.Gather();
  // Test device_free_bytes.
  EXPECT_TRUE(
      dockyard_proxy.CheckValueSent("memory:device_free_bytes", &test_value));
  EXPECT_GT(test_value, 0ULL);
}
