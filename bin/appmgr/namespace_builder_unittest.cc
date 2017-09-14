// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/namespace_builder.h"

#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace app {
namespace {

TEST(NamespaceBuilder, Control) {
  SandboxMetadata sandbox;
  EXPECT_TRUE(sandbox.Parse(R"JSON({
    "dev": [ "class/input", "class/display" ],
    "features": [ "vulkan" ]
  })JSON"));

  NamespaceBuilder builder;
  builder.AddSandbox(sandbox);

  fdio_flat_namespace_t* flat = builder.Build();
  EXPECT_EQ(3u, flat->count);

  std::vector<std::string> paths;
  for (size_t i = 0; i < flat->count; ++i)
    paths.push_back(flat->path[i]);

  EXPECT_TRUE(std::find(paths.begin(), paths.end(), "/dev/class/input") !=
              paths.end());
  EXPECT_TRUE(std::find(paths.begin(), paths.end(), "/dev/class/display") !=
              paths.end());
  EXPECT_TRUE(std::find(paths.begin(), paths.end(), "/system/data/vulkan") !=
              paths.end());

  for (size_t i = 0; i < flat->count; ++i)
    zx_handle_close(flat->handle[i]);
}

}  // namespace
}  // namespace app
