// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sample_bundle.h"

#include "dockyard_proxy_fake.h"
#include "gtest/gtest.h"

class SampleBundleTest : public ::testing::Test {
 public:
  void SetUp() override {}

  harvester::SampleBundle& bundle() { return sample_bundle_; }

 private:
  harvester::SampleBundle sample_bundle_;
};

TEST_F(SampleBundleTest, Bundle) {
  harvester::DockyardProxyFake dockyard_proxy;
  bundle().AddIntSample("koid", 55, "testing:AddIntSample", 42);
  bundle().AddStringSample("koid", 55, "AddStringSample", "answer");
  bundle().Upload(&dockyard_proxy);

  EXPECT_EQ(1U, dockyard_proxy.ValuesSentCount());
  EXPECT_EQ(1U, dockyard_proxy.StringsSentCount());
  EXPECT_EQ(0U, dockyard_proxy.JsonSentCount());

  dockyard::SampleValue value;
  EXPECT_TRUE(
      dockyard_proxy.CheckValueSent("koid:55:testing:AddIntSample", &value));
  EXPECT_EQ(dockyard::SampleValue(42), value);
  EXPECT_FALSE(dockyard_proxy.CheckValueSent("not:sent", &value));

  std::string test_string;
  EXPECT_TRUE(
      dockyard_proxy.CheckStringSent("koid:55:AddStringSample", &test_string));
  EXPECT_EQ("answer", test_string);
  EXPECT_FALSE(dockyard_proxy.CheckStringSent("not:sent", &test_string));
}
