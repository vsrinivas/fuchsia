// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/cmx_metadata.h"

#include "gtest/gtest.h"

#include "third_party/rapidjson/rapidjson/document.h"

namespace component {
namespace {

TEST(CmxMetadata, ParseSandboxMetadata) {
  CmxMetadata cmx;
  rapidjson::Value sandbox;
  EXPECT_TRUE(cmx.ParseSandboxMetadata(
      R"JSON({ "sandbox": { "dev": [ "class/input" ]}, "other": "stuff" })JSON",
      &sandbox));

  EXPECT_TRUE(sandbox.IsObject());
  EXPECT_TRUE(sandbox.HasMember("dev"));
  EXPECT_FALSE(sandbox.HasMember("other"));
}

// Test that we return an error when parsing invalid JSON
TEST(CmxMetadata, ParseInvalidJson) {
  CmxMetadata cmx;
  rapidjson::Value sandbox;
  EXPECT_FALSE(cmx.ParseSandboxMetadata(R"JSON({ ,,, })JSON", &sandbox));
  EXPECT_FALSE(sandbox.IsObject());
}

// Test that we return an error when "sandbox" is missing
TEST(CmxMetadata, ParseMissingSandbox) {
  CmxMetadata cmx;
  rapidjson::Value sandbox;
  EXPECT_FALSE(cmx.ParseSandboxMetadata(
      R"JSON({ "sandwich": { "ingredients": [ "bacon", "lettuce", "tomato" ] } })JSON",
      &sandbox));
  EXPECT_FALSE(sandbox.IsObject());
}

TEST(CmxMetadata, GetCmxPathFromFullPackagePath) {
  EXPECT_EQ("meta/sysmgr.cmx", CmxMetadata::GetCmxPathFromFullPackagePath(
                                   "file:///pkgfs/packages/sysmgr/0"));
  EXPECT_EQ("", CmxMetadata::GetCmxPathFromFullPackagePath(
                    "/pkgfs/packages/sysmgr/0"));
  EXPECT_EQ("", CmxMetadata::GetCmxPathFromFullPackagePath(
                    "file:///pkgfs/nothing/sysmgr/0"));
  EXPECT_EQ("", CmxMetadata::GetCmxPathFromFullPackagePath(""));
}

TEST(CmxMetadata, ExtractRelativeCmxPath) {
  EXPECT_EQ("meta/sysmgr2.cmx",
            CmxMetadata::ExtractRelativeCmxPath(
                "file:///pkgfs/packages/sysmgr/0/meta/sysmgr2.cmx"));
  EXPECT_EQ("meta/sysmgr2.cmx",
            CmxMetadata::ExtractRelativeCmxPath(
                "/pkgfs/packages/sysmgr/0/meta/sysmgr2.cmx"));
  EXPECT_EQ("", CmxMetadata::ExtractRelativeCmxPath(
                    "file:///pkgfs/nothing/sysmgr/0"));
  EXPECT_EQ("", CmxMetadata::ExtractRelativeCmxPath(
                    "file:///pkgfs/packages/sysmgr/0/meta/runtime"));
  EXPECT_EQ("", CmxMetadata::ExtractRelativeCmxPath(
                    "file:///pkgfs/nothing/sysmgr/0/something/sysmgr2.cmx"));
  EXPECT_EQ("", CmxMetadata::ExtractRelativeCmxPath(""));
}

TEST(CmxMetadata, IsCmxExtension) {
  EXPECT_TRUE(CmxMetadata::IsCmxExtension(
      "/pkgfs/packages/component_hello_world/0/meta/hello_world.cmx"));
  EXPECT_FALSE(CmxMetadata::IsCmxExtension(
      "/pkgfs/packages/component_hello_world/0/bin/app"));
  EXPECT_TRUE(CmxMetadata::IsCmxExtension("meta/hello_world.cmx"));
  EXPECT_FALSE(CmxMetadata::IsCmxExtension("bin/app"));
}

TEST(CmxMetadata, GetPackageNameFromCmxPath) {
  EXPECT_EQ(
      "component_hello_world",
      CmxMetadata::GetPackageNameFromCmxPath(
          "/pkgfs/packages/component_hello_world/0/meta/hello_world.cmx"));
  EXPECT_EQ("", CmxMetadata::GetPackageNameFromCmxPath(
                    "/pkgfs/packages/component_hello_world/0/bin/app"));
  EXPECT_EQ("",
            CmxMetadata::GetPackageNameFromCmxPath(
                "/pkgfs/nothing/component_hello_world/0/meta/hello_world.cmx"));
  EXPECT_EQ("", CmxMetadata::GetPackageNameFromCmxPath(
                    "/pkgfs/packages//0/meta/hello_world.cmx"));
}

}  // namespace
}  // namespace component
