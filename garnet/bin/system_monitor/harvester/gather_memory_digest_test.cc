// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_memory_digest.h"

#include "dockyard_proxy_fake.h"
#include "gtest/gtest.h"
#include "root_resource.h"

class GatherMemoryDigestTest : public ::testing::Test {};

TEST_F(GatherMemoryDigestTest, Inspectable) {
  zx_handle_t root_resource;
  zx_status_t ret = harvester::GetRootResource(&root_resource);
  ASSERT_EQ(ret, ZX_OK);
  harvester::DockyardProxyFake dockyard_proxy;

  harvester::GatherMemoryDigest gatherer(root_resource, &dockyard_proxy);
  uint64_t test_value;

  // No samples are currently gathered in this call. Test that  this is
  // available to call and doesn't crash.
  gatherer.GatherDeviceProperties();

  gatherer.Gather();
  // Test memory_digest:kernel.
  EXPECT_TRUE(
      dockyard_proxy.CheckValueSent("memory_digest:kernel", &test_value));
  EXPECT_GT(test_value, 0ULL);
}
