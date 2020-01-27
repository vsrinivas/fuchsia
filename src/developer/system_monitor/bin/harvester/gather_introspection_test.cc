// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_introspection.h"

#include "dockyard_proxy_fake.h"
#include "gtest/gtest.h"

class GatherIntrospectionTest : public ::testing::Test {
 public:
  void SetUp() override {}
};

TEST_F(GatherIntrospectionTest, Introspection) {
  zx_handle_t root_resource = 0;
  harvester::DockyardProxyFake dockyard_proxy;

  harvester::GatherIntrospection gatherer(root_resource, &dockyard_proxy);
  gatherer.Gather();
  std::string test_string;
  EXPECT_TRUE(dockyard_proxy.CheckJsonSent("inspect:/hub/fake/234/faux.Inspect",
                                           &test_string));
  EXPECT_EQ("{ \"test\": 5 }", test_string);
}
