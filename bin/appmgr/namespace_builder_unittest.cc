// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/namespace_builder.h"

#include <zx/channel.h>

#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace component {
namespace {

TEST(NamespaceBuilder, Control) {
  SandboxMetadata sandbox;
  EXPECT_TRUE(sandbox.Parse(R"JSON({
    "dev": [ "class/input", "class/display-controller" ],
    "features": [ "vulkan" ]
  })JSON"));

  NamespaceBuilder builder;
  builder.AddSandbox(sandbox, [] { return zx::channel(); });

  fdio_flat_namespace_t* flat = builder.Build();
  EXPECT_EQ(5u, flat->count);

  std::vector<std::string> paths;
  for (size_t i = 0; i < flat->count; ++i)
    paths.push_back(flat->path[i]);

  EXPECT_TRUE(std::find(paths.begin(), paths.end(), "/dev/class/input") !=
              paths.end());
  EXPECT_TRUE(std::find(paths.begin(), paths.end(),
                        "/dev/class/display-controller") != paths.end());
  EXPECT_TRUE(std::find(paths.begin(), paths.end(), "/dev/class/gpu") !=
              paths.end());
  EXPECT_TRUE(std::find(paths.begin(), paths.end(), "/system/data/vulkan") !=
              paths.end());
  EXPECT_TRUE(std::find(paths.begin(), paths.end(), "/system/lib") !=
              paths.end());

  for (size_t i = 0; i < flat->count; ++i)
    zx_handle_close(flat->handle[i]);
}

TEST(NamespaceBuilder, Shell) {
  SandboxMetadata sandbox;
  EXPECT_TRUE(sandbox.Parse(R"JSON({
    "features": [ "shell" ]
  })JSON"));

  NamespaceBuilder builder;
  builder.AddSandbox(sandbox, [] { return zx::channel(); });

  fdio_flat_namespace_t* flat = builder.Build();
  EXPECT_EQ(11u, flat->count);

  std::vector<std::string> paths;
  for (size_t i = 0; i < flat->count; ++i)
    paths.push_back(flat->path[i]);

  // /config/ssl is included because "shell" implies "root-ssl-certificates"
  EXPECT_TRUE(std::find(paths.begin(), paths.end(), "/config/ssl") !=
              paths.end());
  // While "shell" implies "root-ssl-certificates", it does NOT include /system/data/boringssl
  // (see comment in namespace_builder.cc for details).
  EXPECT_TRUE(std::find(paths.begin(), paths.end(), "/system/data/boringssl") ==
              paths.end());

  // Paths that are only part of "shell", not "root-ssl-certificates"
  EXPECT_TRUE(std::find(paths.begin(), paths.end(), "/blob") !=
              paths.end());
  EXPECT_TRUE(std::find(paths.begin(), paths.end(), "/boot") !=
              paths.end());
  EXPECT_TRUE(std::find(paths.begin(), paths.end(), "/data") !=
              paths.end());
  EXPECT_TRUE(std::find(paths.begin(), paths.end(), "/dev") !=
              paths.end());
  EXPECT_TRUE(std::find(paths.begin(), paths.end(), "/hub") !=
              paths.end());
  EXPECT_TRUE(std::find(paths.begin(), paths.end(), "/install") !=
              paths.end());
  EXPECT_TRUE(std::find(paths.begin(), paths.end(), "/pkgfs") !=
              paths.end());
  EXPECT_TRUE(std::find(paths.begin(), paths.end(), "/system") !=
              paths.end());
  EXPECT_TRUE(std::find(paths.begin(), paths.end(), "/tmp") !=
              paths.end());
  EXPECT_TRUE(std::find(paths.begin(), paths.end(), "/volume") !=
              paths.end());

  for (size_t i = 0; i < flat->count; ++i)
    zx_handle_close(flat->handle[i]);
}

}  // namespace
}  // namespace component
