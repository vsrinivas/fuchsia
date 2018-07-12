// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/scheme_map.h"

#include <string>

#include "gtest/gtest.h"

namespace component {
namespace {

void ExpectFailedParse(const std::string& expected_error,
                       const std::string& json) {
  SchemeMap scheme_map;
  std::string error;
  EXPECT_FALSE(scheme_map.Parse(json, &error));
  EXPECT_EQ(error, expected_error);
}

TEST(SchemeMap, ParseAndLookUp) {
  SchemeMap scheme_map;
  std::string error;
  ASSERT_TRUE(scheme_map.Parse(
      R"JSON({
        "launchers": {
          "network": [ "http", "https" ],
          "package": [ "file" ]
        }
      })JSON",
      &error)) << error;
  EXPECT_EQ("", error);
  EXPECT_EQ("network", scheme_map.LookUp("http"));
  EXPECT_EQ("network", scheme_map.LookUp("https"));
  EXPECT_EQ("package", scheme_map.LookUp("file"));
  EXPECT_EQ("", scheme_map.LookUp("doofus"));
}

TEST(SchemeMap, ParseInvalid) {
  ExpectFailedParse(
      "document is not a valid object",
      R"JSON()JSON");
  ExpectFailedParse(
      "missing \"launchers\"",
      R"JSON({})JSON");
  ExpectFailedParse(
      "\"launchers\" is not a valid object",
      R"JSON({ "launchers": 42 })JSON");
  ExpectFailedParse(
      "schemes for \"network\" are not a list",
      R"JSON({
        "launchers": {
          "network": "http"
        }
      })JSON");
  ExpectFailedParse(
      "scheme for \"network\" is not a string",
      R"JSON({
        "launchers": {
          "network": [ "http", 42 ]
        }
      })JSON");
}

TEST(SchemeMap, GetSchemeMapPath) {
  EXPECT_EQ("/system/data/appmgr/scheme_map.config",
            SchemeMap::GetSchemeMapPath());
}

}  // namespace
}  // namespace component