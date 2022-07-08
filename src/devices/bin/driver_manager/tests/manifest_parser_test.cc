// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/manifest_parser.h"

#include <zxtest/zxtest.h>

TEST(ManifestParserTest, BootUrl) {
  json_parser::JSONParser parser;
  auto doc = parser.ParseFromString(
      "[ { \"driver_url\": \"fuchsia-boot:///#driver/my-driver.so\"} ]", "test");
  ASSERT_FALSE(parser.HasError());

  auto result = ParseDriverManifest(std::move(doc));
  ASSERT_EQ(result.status_value(), ZX_OK);
  ASSERT_EQ(result.value().size(), 1);
  ASSERT_EQ(result.value()[0].driver_url, "fuchsia-boot:///#driver/my-driver.so");
}

TEST(ManifestParserTest, FuchsiaUrl) {
  json_parser::JSONParser parser;
  auto doc = parser.ParseFromString(
      "[ { \"driver_url\": \"fuchsia-pkg://fuchsia.com/my-package#driver/my-driver.so\"} ]",
      "test");
  ASSERT_FALSE(parser.HasError());

  auto result = ParseDriverManifest(std::move(doc));
  ASSERT_EQ(result.status_value(), ZX_OK);
  ASSERT_EQ(result.value().size(), 1);
  ASSERT_EQ(result.value()[0].driver_url,
            "fuchsia-pkg://fuchsia.com/my-package#driver/my-driver.so");
}

TEST(ManifestParserTest, FuchsiaUrlToPath) {
  auto result = GetPathFromUrl("fuchsia-pkg://fuchsia.com/my-package#driver/my-driver.so");
  ASSERT_EQ(result.status_value(), ZX_OK);
  ASSERT_EQ(result.value(), "/pkgfs/packages/my-package/0/driver/my-driver.so");
}

TEST(ManifestParserTest, BootUrlToPath) {
  auto result = GetPathFromUrl("fuchsia-boot:///#driver/my-driver.so");
  ASSERT_EQ(result.status_value(), ZX_OK);
  ASSERT_EQ(result.value(), "/boot/driver/my-driver.so");
}

TEST(ManifestParserTest, FuchsiaUrlToBasePath) {
  auto result = GetBasePathFromUrl("fuchsia-pkg://fuchsia.com/my-package#driver/my-driver.so");
  ASSERT_EQ(result.status_value(), ZX_OK);
  ASSERT_EQ(result.value(), "/pkgfs/packages/my-package/0");
}

TEST(ManifestParserTest, BootUrlToBasePath) {
  auto result = GetBasePathFromUrl("fuchsia-boot:///#driver/my-driver.so");
  ASSERT_EQ(result.status_value(), ZX_OK);
  ASSERT_EQ(result.value(), "/boot");
}
