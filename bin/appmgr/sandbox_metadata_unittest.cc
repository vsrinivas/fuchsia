// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/sandbox_metadata.h"

#include "gtest/gtest.h"

namespace app {
namespace {

TEST(SandboxMetadata, Parse) {
  SandboxMetadata sandbox;
  EXPECT_FALSE(sandbox.Parse(""));
  EXPECT_TRUE(sandbox.Parse("{}"));

  EXPECT_TRUE(sandbox.Parse(R"JSON({ "dev": [ "class/input" ]})JSON"));
  EXPECT_EQ(1u, sandbox.dev().size());
  EXPECT_EQ(0u, sandbox.features().size());
  EXPECT_EQ("class/input", sandbox.dev()[0]);

  EXPECT_TRUE(sandbox.Parse(R"JSON({ "features": [ "vulkan" ]})JSON"));
  EXPECT_EQ(0u, sandbox.dev().size());
  EXPECT_EQ(1u, sandbox.features().size());
  EXPECT_EQ("vulkan", sandbox.features()[0]);
}

}  // namespace
}  // namespace app
