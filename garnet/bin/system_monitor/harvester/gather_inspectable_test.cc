// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_inspectable.h"

#include "dockyard_proxy_fake.h"
#include "gtest/gtest.h"

class GatherInspectableTest : public ::testing::Test {
 public:
  void SetUp() override {}
};

TEST_F(GatherInspectableTest, Inspectable) {
  zx_handle_t root_resource = 0;
  harvester::DockyardProxyFake dockyard_proxy;

  harvester::GatherInspectable gatherer(root_resource, &dockyard_proxy);
  gatherer.Gather();
  std::string test_string;
  EXPECT_TRUE(dockyard_proxy.CheckStringPrefixSent(
      "inspectable:/hub/c/system_monitor_harvester_test.cmx/", &test_string));
  EXPECT_EQ("fuchsia.inspect.deprecated.Inspect", test_string);
}
