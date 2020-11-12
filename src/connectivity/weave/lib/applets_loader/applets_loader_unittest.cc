// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/weave/lib/applets_loader/applets_loader.h"

#include <gtest/gtest.h>

#include "src/connectivity/weave/lib/applets_loader/testing/applets_loader_test_base.h"

namespace weavestack::applets {
namespace {

class AppletsLoaderTest : public testing::AppletsLoaderTestBase {};

// The |AppletsLoaderModuleNotLoadedTest| suite holds tests that exercise the `AppletsLoader` in a
// state before a valid module has been loaded. This is done by the test fixture for
// |AppletsLoaderTest| so don't use the fixture for these test cases.
TEST(AppletsLoaderModuleNotLoadedTest, CreateWithInvalidModule) {
  std::unique_ptr<AppletsLoader> loader;
  EXPECT_EQ(ZX_ERR_UNAVAILABLE, AppletsLoader::CreateWithModule("does_not_exist.so", &loader));
  EXPECT_FALSE(loader);
}

TEST(AppletsLoaderModuleNotLoadedTest, CreateWithNullModule) {
  // Sanity test the null module behaves as expected.
  std::unique_ptr<AppletsLoader> loader = AppletsLoader::CreateWithNullModule();
  EXPECT_TRUE(loader);
}

}  // namespace
}  // namespace weavestack::applets
