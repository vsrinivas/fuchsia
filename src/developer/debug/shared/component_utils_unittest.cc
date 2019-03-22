// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/component_utils.h"

#include <gtest/gtest.h>

namespace debug_ipc {

namespace {

#define PACKAGE "some-package"
#define COMPONENT "some-component"

const char kAlmostUrl[] = "fuchsia-pkg://fuchsia.com/asfsad/meta/asda.cmx";
const char kActualUrl[] =
    "fuchsia-pkg://fuchsia.com/" PACKAGE "#meta/" COMPONENT ".cmx";

}  // namespace

TEST(ComponentUtils, ExtractComponentFromPackageUrl) {
  ComponentDescription desc;

  EXPECT_FALSE(ExtractComponentFromPackageUrl("asgssf", &desc));
  EXPECT_TRUE(desc.package_name.empty());
  EXPECT_TRUE(desc.component_name.empty());

  EXPECT_FALSE(ExtractComponentFromPackageUrl(kAlmostUrl, &desc));
  EXPECT_TRUE(desc.package_name.empty());
  EXPECT_TRUE(desc.component_name.empty());

  EXPECT_TRUE(ExtractComponentFromPackageUrl(kActualUrl, &desc));
  EXPECT_EQ(desc.package_name, PACKAGE);
  EXPECT_EQ(desc.component_name, COMPONENT);
}

}  // namespace debug_ipc
