// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/scheme_map.h"

#include <string>

#include "gtest/gtest.h"
#include "lib/fxl/files/path.h"
#include "lib/fxl/files/scoped_temp_dir.h"

namespace component {
namespace {

class SchemeMapTest : public ::testing::Test {
 protected:
  void ExpectFailedParse(const std::string& json,
                         std::string expected_error) {
    SchemeMap scheme_map;
    std::string error;
    std::string json_file;
    EXPECT_FALSE(ParseFrom(&scheme_map, json, &json_file));
    if (scheme_map.HasError()) {
      error = scheme_map.error_str();
    }
    // TODO(DX-338): Use strings/substitute.h once that actually exists in fxl.
    size_t pos;
    while ((pos = expected_error.find("$0")) != std::string::npos) {
      expected_error.replace(pos, 2, json_file);
    }
    EXPECT_EQ(error, expected_error);
  }


  bool ParseFrom(SchemeMap* scheme_map, const std::string& json,
                 std::string* json_file) {
    std::string json_path;
    if (!tmp_dir_.NewTempFileWithData(json, json_file)) {
      return false;
    }
    return scheme_map->ParseFromFile(*json_file);
  }

 private:
  files::ScopedTempDir tmp_dir_;
};

const char kSchemeMapStr[] =
    R"JSON({
              "launchers": {
                "web_runner": [ "http", "https" ],
                "package": [ "file" ]
              }
           })JSON";

TEST_F(SchemeMapTest, Parse) {
  SchemeMap scheme_map;
  std::string file_unused;
  ASSERT_TRUE(ParseFrom(&scheme_map, kSchemeMapStr, &file_unused));
  EXPECT_FALSE(scheme_map.HasError());
  EXPECT_EQ("web_runner", scheme_map.LookUp("http"));
  EXPECT_EQ("web_runner", scheme_map.LookUp("https"));
  EXPECT_EQ("package", scheme_map.LookUp("file"));
  EXPECT_EQ("", scheme_map.LookUp("doofus"));
}

TEST_F(SchemeMapTest, ParseWithErrors) {
  ExpectFailedParse(R"JSON({})JSON", "$0: Missing 'launchers'.");
  ExpectFailedParse(R"JSON({ "launchers": 42 })JSON",
                    "$0: 'launchers' is not a valid object.");
  ExpectFailedParse(
      R"JSON({
        "launchers": {
          "web_runner": "http"
        }
      })JSON",
      "$0: Schemes for 'web_runner' are not a list."),
  ExpectFailedParse(
      R"JSON({
        "launchers": {
          "package": [ "file" ],
          "web_runner": [ "http", 42 ]
        }
      })JSON",
      "$0: Scheme for 'web_runner' is not a string.");
}

TEST_F(SchemeMapTest, GetSchemeMapPath) {
  EXPECT_EQ("/system/data/appmgr/scheme_map.config",
            SchemeMap::GetSchemeMapPath());
}

}  // namespace
}  // namespace component