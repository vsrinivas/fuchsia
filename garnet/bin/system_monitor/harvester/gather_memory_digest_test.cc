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

  // No samples are currently gathered in this call. Test that  this is
  // available to call and doesn't crash.
  gatherer.GatherDeviceProperties();

  gatherer.Gather();
  uint64_t test_value;
  // Test memory_digest:kernel.
  EXPECT_TRUE(
      dockyard_proxy.CheckValueSent("memory_digest:kernel", &test_value));
  EXPECT_GT(test_value, 0ULL);
  // Test summary bytes for the kernel.
  uint64_t private_bytes;
  EXPECT_TRUE(dockyard_proxy.CheckValueSent("koid:1:summary:private_bytes",
                                            &private_bytes));
  EXPECT_GT(private_bytes, 0ULL);
  uint64_t scaled_bytes;
  EXPECT_TRUE(dockyard_proxy.CheckValueSent("koid:1:summary:scaled_bytes",
                                            &scaled_bytes));
  EXPECT_GT(scaled_bytes, 0ULL);
  uint64_t total_bytes;
  EXPECT_TRUE(dockyard_proxy.CheckValueSent("koid:1:summary:total_bytes",
                                            &total_bytes));
  EXPECT_GT(total_bytes, 0ULL);
  // The scaled_bytes may vary from private_bytes to total_bytes. If the object
  // has no shared memory then all three values will be equal.
  EXPECT_GE(total_bytes, scaled_bytes);
  EXPECT_GE(scaled_bytes, private_bytes);
}
