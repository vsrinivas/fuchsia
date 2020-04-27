// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/pkg_url/fuchsia_pkg_url.h"

#include <gtest/gtest.h>

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
  EXPECT_FALSE(fp.Parse("file://fuchsia.com/component_hello_world#meta/hello_world.cmx"));
  EXPECT_FALSE(fp.Parse("#meta/stuff"));
  EXPECT_FALSE(fp.Parse("fuchsia-pkg://fuchsia.com/component_hello_world#"));

  EXPECT_TRUE(fp.Parse("fuchsia-pkg://fuchsia.com/component_hello_world"));
  EXPECT_EQ("fuchsia.com", fp.host_name());
  EXPECT_EQ("component_hello_world", fp.package_name());
  EXPECT_EQ("0", fp.variant());
  EXPECT_EQ("", fp.hash());
  EXPECT_EQ("", fp.resource_path());
  EXPECT_EQ("fuchsia-pkg://fuchsia.com/component_hello_world/0", fp.package_path());

  EXPECT_TRUE(fp.Parse("fuchsia-pkg://fuchsia.com/component_hello_world#meta/hello_world.cmx"));
  EXPECT_EQ("fuchsia.com", fp.host_name());
  EXPECT_EQ("component_hello_world", fp.package_name());
  EXPECT_EQ("0", fp.variant());
  EXPECT_EQ("", fp.hash());
  EXPECT_EQ("meta/hello_world.cmx", fp.resource_path());
  EXPECT_EQ("fuchsia-pkg://fuchsia.com/component_hello_world/0", fp.package_path());

  EXPECT_TRUE(fp.Parse("fuchsia-pkg://fuchsia.com/component_hello_world#meta/stuff"));
  EXPECT_EQ("fuchsia.com", fp.host_name());
  EXPECT_EQ("component_hello_world", fp.package_name());
  EXPECT_EQ("0", fp.variant());
  EXPECT_EQ("", fp.hash());
  EXPECT_EQ("meta/stuff", fp.resource_path());
  EXPECT_EQ("fuchsia-pkg://fuchsia.com/component_hello_world/0", fp.package_path());

  EXPECT_TRUE(fp.Parse("fuchsia-pkg://example.com/data-package#stuff"));
  EXPECT_EQ("example.com", fp.host_name());
  EXPECT_EQ("data-package", fp.package_name());
  EXPECT_EQ("0", fp.variant());
  EXPECT_EQ("", fp.hash());
  EXPECT_EQ("stuff", fp.resource_path());
  EXPECT_EQ("fuchsia-pkg://example.com/data-package/0", fp.package_path());

  EXPECT_TRUE(fp.Parse("fuchsia-pkg://example.com/data-package/variant123#stuff"));
  EXPECT_EQ("example.com", fp.host_name());
  EXPECT_EQ("data-package", fp.package_name());
  EXPECT_EQ("variant123", fp.variant());
  EXPECT_EQ("", fp.hash());
  EXPECT_EQ("stuff", fp.resource_path());
  EXPECT_EQ("fuchsia-pkg://example.com/data-package/variant123", fp.package_path());

  EXPECT_TRUE(fp.Parse("fuchsia-pkg://example.com/data-package/variant123?hash=1234#stuff"));
  EXPECT_EQ("example.com", fp.host_name());
  EXPECT_EQ("data-package", fp.package_name());
  EXPECT_EQ("variant123", fp.variant());
  EXPECT_EQ("1234", fp.hash());
  EXPECT_EQ("stuff", fp.resource_path());
  EXPECT_EQ("fuchsia-pkg://example.com/data-package/variant123?hash=1234", fp.package_path());
}

TEST(FuchsiaPkgUrl, Equality) {
  {
    FuchsiaPkgUrl fp1;
    ASSERT_TRUE(fp1.Parse("fuchsia-pkg://fuchsia.com/hello"));
    auto fp2 = fp1;
    FuchsiaPkgUrl fp3;
    ASSERT_TRUE(fp3.Parse("fuchsia-pkg://fuchsia.com/goodbye"));
    EXPECT_EQ(fp1, fp2);
    EXPECT_NE(fp1, fp3);
  }
  {
    FuchsiaPkgUrl fp1;
    ASSERT_TRUE(fp1.Parse("fuchsia-pkg://fuchsia.com/hello#meta/hello.cmx"));
    auto fp2 = fp1;
    FuchsiaPkgUrl fp3;
    ASSERT_TRUE(fp3.Parse("fuchsia-pkg://fuchsia.com/hello"));
    FuchsiaPkgUrl fp4;
    ASSERT_TRUE(fp4.Parse("fuchsia-pkg://fuchsia.com/hello#meta/goodbye.cmx"));
    EXPECT_EQ(fp1, fp2);
    EXPECT_NE(fp1, fp3);
    EXPECT_NE(fp1, fp4);
  }
  {
    FuchsiaPkgUrl fp1;
    ASSERT_TRUE(fp1.Parse("fuchsia-pkg://fuchsia.com/hello/1#meta/hello.cmx"));
    auto fp2 = fp1;
    FuchsiaPkgUrl fp3;
    ASSERT_TRUE(fp3.Parse("fuchsia-pkg://fuchsia.com/hello#meta/hello.cmx"));
    FuchsiaPkgUrl fp4;
    ASSERT_TRUE(fp4.Parse("fuchsia-pkg://fuchsia.com/hello/2#meta/hello.cmx"));
    EXPECT_EQ(fp1, fp2);
    EXPECT_NE(fp1, fp3);
    EXPECT_NE(fp1, fp4);
  }
  {
    FuchsiaPkgUrl fp1;
    ASSERT_TRUE(fp1.Parse("fuchsia-pkg://fuchsia.com/hello/1?hash=123#meta/hello.cmx"));
    auto fp2 = fp1;
    FuchsiaPkgUrl fp3;
    ASSERT_TRUE(fp3.Parse("fuchsia-pkg://fuchsia.com/hello/1#meta/hello.cmx"));
    FuchsiaPkgUrl fp4;
    ASSERT_TRUE(fp4.Parse("fuchsia-pkg://fuchsia.com/hello/1?hash=456#meta/hello.cmx"));
    EXPECT_EQ(fp1, fp2);
    EXPECT_NE(fp1, fp3);
    EXPECT_NE(fp1, fp4);
  }
}

TEST(FuchsiaPkgUrl, pkgfs_dir_path) {
  FuchsiaPkgUrl fp;
  EXPECT_TRUE(fp.Parse("fuchsia-pkg://fuchsia.com/component_hello_world#meta/hello_world.cmx"));
  EXPECT_EQ("/pkgfs/packages/component_hello_world/0", fp.pkgfs_dir_path());

  EXPECT_TRUE(
      fp.Parse("fuchsia-pkg://fuchsia.com/component_hello_world/variant123#meta/"
               "hello_world.cmx"));
  EXPECT_EQ("/pkgfs/packages/component_hello_world/variant123", fp.pkgfs_dir_path());
}

TEST(FuchsiaPkgUrl, GetComponentDefaults) {
  EXPECT_EQ("meta/sysmgr.cmx",
            ParseFuchsiaPkgUrl("fuchsia-pkg://fuchsia.com/sysmgr").GetDefaultComponentCmxPath());
  EXPECT_EQ("meta/sysmgr.cmx", ParseFuchsiaPkgUrl("fuchsia-pkg://fuchsia.com/sysmgr#meta/blah.cmx")
                                   .GetDefaultComponentCmxPath());
}

}  // namespace
}  // namespace component
