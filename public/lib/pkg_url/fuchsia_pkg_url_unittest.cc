// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/pkg_url/fuchsia_pkg_url.h"

#include "gtest/gtest.h"

namespace component {
namespace {

FuchsiaPkgUrl ParseFuchsiaPkgUrl(const std::string& s) {
  FuchsiaPkgUrl url;
  EXPECT_TRUE(url.Parse(s));
  return url;
}

TEST(FuchsiaPkgUrl, Parse) {
  FuchsiaPkgUrl fp;
  EXPECT_FALSE(fp.Parse(""));
  EXPECT_FALSE(fp.Parse("{}"));
  EXPECT_FALSE(fp.Parse(
      "file://fuchsia.com/component_hello_world#meta/hello_world.cmx"));
  EXPECT_FALSE(fp.Parse("#meta/stuff"));
  EXPECT_FALSE(fp.Parse("fuchsia-pkg://fuchsia.com/component_hello_world#"));

  EXPECT_TRUE(fp.Parse("fuchsia-pkg://fuchsia.com/component_hello_world"));
  EXPECT_EQ("component_hello_world", fp.package_name());
  EXPECT_EQ("", fp.resource_path());

  EXPECT_TRUE(fp.Parse(
      "fuchsia-pkg://fuchsia.com/component_hello_world#meta/hello_world.cmx"));
  EXPECT_EQ("component_hello_world", fp.package_name());
  EXPECT_EQ("meta/hello_world.cmx", fp.resource_path());

  EXPECT_TRUE(
      fp.Parse("fuchsia-pkg://fuchsia.com/component_hello_world#meta/stuff"));
  EXPECT_EQ("component_hello_world", fp.package_name());
  EXPECT_EQ("meta/stuff", fp.resource_path());

  EXPECT_TRUE(
      fp.Parse("fuchsia-pkg://example.com/data-package#stuff"));
  EXPECT_EQ("data-package", fp.package_name());
  EXPECT_EQ("stuff", fp.resource_path());

  EXPECT_TRUE(
      fp.Parse("fuchsia-pkg://example.com/data-package/variant#stuff"));
  EXPECT_EQ("data-package", fp.package_name());
  EXPECT_EQ("stuff", fp.resource_path());
}

TEST(FuchsiaPkgUrl, pkgfs_dir_path) {
  FuchsiaPkgUrl fp;
  EXPECT_TRUE(fp.Parse(
      "fuchsia-pkg://fuchsia.com/component_hello_world#meta/hello_world.cmx"));
  EXPECT_EQ("/pkgfs/packages/component_hello_world/0", fp.pkgfs_dir_path());
}

TEST(FuchsiaPkgUrl, GetComponentDefaults) {
  EXPECT_EQ("meta/sysmgr.cmx",
            ParseFuchsiaPkgUrl("fuchsia-pkg://fuchsia.com/sysmgr")
                .GetDefaultComponentCmxPath());
  EXPECT_EQ("meta/sysmgr.cmx",
            ParseFuchsiaPkgUrl("fuchsia-pkg://fuchsia.com/sysmgr#meta/blah.cmx")
                .GetDefaultComponentCmxPath());
  EXPECT_EQ("sysmgr", ParseFuchsiaPkgUrl("fuchsia-pkg://fuchsia.com/sysmgr")
                          .GetDefaultComponentName());
  EXPECT_EQ("sysmgr",
            ParseFuchsiaPkgUrl("fuchsia-pkg://fuchsia.com/sysmgr#meta/blah.cmx")
                .GetDefaultComponentName());
}

}  // namespace
}  // namespace component
