// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/weave/lib/applets_loader/testing/applets_loader_test_base.h"

#include <dlfcn.h>

#include "src/connectivity/weave/applets/test_applets/test_applets.h"
#include "src/connectivity/weave/lib/applets_loader/testing/test_applets.h"

namespace weavestack::applets::testing {

void AppletsLoaderTestBase::SetUp() {
  ::testing::Test::SetUp();
  RecreateLoader();
}

void AppletsLoaderTestBase::TearDown() {
  ASSERT_EQ(0u, test_applets_.InstanceCount());
  ::testing::Test::TearDown();
}

void AppletsLoaderTestBase::RecreateLoader() {
  ASSERT_EQ(AppletsLoader::CreateWithModule(kTestAppletsModuleName, &applets_loader_), ZX_OK);
  ASSERT_TRUE(applets_loader_);
}

}  // namespace weavestack::applets::testing
