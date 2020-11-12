// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/weave/lib/applets_loader/applets_module.h"

#include <gtest/gtest.h>

#include "src/connectivity/weave/lib/applets_loader/testing/applets_loader_test_base.h"

namespace weavestack::applets {
namespace {

TEST(AppletsModuleTest, OpenModule) {
  auto m = AppletsModuleV1::Open(testing::kTestAppletsModuleName);
  ASSERT_TRUE(m);
}

TEST(AppletsModuleTest, MoveModule) {
  auto m1 = AppletsModuleV1::Open(testing::kTestAppletsModuleName);
  ASSERT_TRUE(m1);

  auto m2 = std::move(m1);
  EXPECT_FALSE(m1);
  EXPECT_TRUE(m2);
}

}  // namespace
}  // namespace weavestack::applets
