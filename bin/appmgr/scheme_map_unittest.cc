// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/scheme_map.h"

#include <cstdio>
#include <string>

#include "gtest/gtest.h"
#include "lib/fxl/files/path.h"
#include "lib/fxl/files/scoped_temp_dir.h"

namespace component {
namespace {

void ExpectFailedParse(const std::string& expected_error,
                       const std::string& json) {
  SchemeMap scheme_map;
  std::string error;
  EXPECT_FALSE(scheme_map.Parse(json, &error));
  EXPECT_EQ(error, expected_error);
}

const char kSchemeMapStr[] =
    R"JSON({
              "launchers": {
                "web_runner": [ "http", "https" ],
                "package": [ "file" ]
              }
           })JSON";

TEST(SchemeMap, ParseAndLookUp) {
  SchemeMap scheme_map;
  std::string error;
  ASSERT_TRUE(scheme_map.Parse(kSchemeMapStr, &error)) << error;
  EXPECT_EQ("", error);
  EXPECT_EQ("web_runner", scheme_map.LookUp("http"));
  EXPECT_EQ("web_runner", scheme_map.LookUp("https"));
  EXPECT_EQ("package", scheme_map.LookUp("file"));
  EXPECT_EQ("", scheme_map.LookUp("doofus"));
}

TEST(SchemeMap, ReadAndLookUp) {
  files::ScopedTempDir dir;
  std::string tmp_file;
  ASSERT_TRUE(dir.NewTempFile(&tmp_file));

  FILE* tmpf = std::fopen(tmp_file.c_str(), "w");
  std::fprintf(tmpf, "%s", kSchemeMapStr);
  std::fclose(tmpf);

  SchemeMap scheme_map;
  std::string error;
  ASSERT_TRUE(scheme_map.ReadFrom(tmp_file, &error)) << error;
  EXPECT_EQ("", error);
  EXPECT_EQ("web_runner", scheme_map.LookUp("http"));
  EXPECT_EQ("web_runner", scheme_map.LookUp("https"));
  EXPECT_EQ("package", scheme_map.LookUp("file"));
  EXPECT_EQ("", scheme_map.LookUp("doofus"));

  files::DeletePath(tmp_file, false);
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
      "schemes for \"web_runner\" are not a list",
      R"JSON({
        "launchers": {
          "web_runner": "http"
        }
      })JSON");
  ExpectFailedParse(
      "scheme for \"web_runner\" is not a string",
      R"JSON({
        "launchers": {
          "web_runner": [ "http", 42 ]
        }
      })JSON");
}

TEST(SchemeMap, GetSchemeMapPath) {
  EXPECT_EQ("/system/data/appmgr/scheme_map.config",
            SchemeMap::GetSchemeMapPath());
}

}  // namespace
}  // namespace component