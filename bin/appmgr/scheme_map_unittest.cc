// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/scheme_map.h"

#include <unistd.h>
#include <string>

#include "gtest/gtest.h"
#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/path.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/strings/concatenate.h"

namespace component {
namespace {

class SchemeMapTest : public ::testing::Test {
 protected:
  void ExpectFailedParse(const std::string& json, std::string expected_error) {
    SchemeMap scheme_map;
    std::string dir;
    ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
    const std::string json_file = NewJSONFile(dir, json);
    EXPECT_FALSE(scheme_map.ParseFromDirectory(dir));
    // TODO(DX-338): Use strings/substitute.h once that actually exists in fxl.
    size_t pos;
    while ((pos = expected_error.find("$0")) != std::string::npos) {
      expected_error.replace(pos, 2, json_file);
    }
    EXPECT_EQ(scheme_map.error_str(), expected_error);
  }

  std::string NewJSONFile(const std::string& dir, const std::string& json) {
    const std::string json_file =
        fxl::Concatenate({dir, "/json_file", std::to_string(unique_id_++)});
    if (!files::WriteFile(json_file, json.data(), json.size())) {
      return "";
    }
    return json_file;
  }

  files::ScopedTempDir tmp_dir_;

 private:
  int unique_id_ = 1;
};

TEST_F(SchemeMapTest, Parse) {
  static constexpr char kJson[] = R"JSON({
  "launchers": {
    "web_runner": [ "http", "https" ],
    "package": [ "file" ]
  }
  })JSON";

  SchemeMap scheme_map;
  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  NewJSONFile(dir, kJson);
  EXPECT_TRUE(scheme_map.ParseFromDirectory(dir));
  EXPECT_FALSE(scheme_map.HasError());
  EXPECT_EQ("web_runner", scheme_map.LookUp("http"));
  EXPECT_EQ("web_runner", scheme_map.LookUp("https"));
  EXPECT_EQ("package", scheme_map.LookUp("file"));
  EXPECT_EQ("", scheme_map.LookUp("doofus"));
}

TEST_F(SchemeMapTest, ParseMultiple) {
  static constexpr char kJson1[] = R"JSON({
  "launchers": { "web_runner": [ "http" ] }
  })JSON";
  static constexpr char kJson2[] = R"JSON({
  "launchers": { "web_runner": [ "https" ] }
  })JSON";
  static constexpr char kJson3[] = R"JSON({
  "launchers": { "package": [ "file" ] }
  })JSON";

  SchemeMap scheme_map;
  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  NewJSONFile(dir, kJson1);
  NewJSONFile(dir, kJson2);
  NewJSONFile(dir, kJson3);
  EXPECT_TRUE(scheme_map.ParseFromDirectory(dir));
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

TEST_F(SchemeMapTest, ParseMultipleWithErrors) {
  static constexpr char kJson1[] = R"JSON({
  "launchers": { "web_runner": [ "http" ] }
  })JSON";
  static constexpr char kJson2[] = R"JSON({
  "launchers": { "package": [ "http" ] }
  })JSON";

  SchemeMap scheme_map;
  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  NewJSONFile(dir, kJson1);
  const std::string json_file2 = NewJSONFile(dir, kJson2);
  EXPECT_FALSE(scheme_map.ParseFromDirectory(dir));
  EXPECT_TRUE(scheme_map.HasError());
  // TODO(DX-338): Use strings/substitute.h once that actually exists in fxl.
  const std::string expected_error = fxl::Concatenate(
      {json_file2, ": Scheme 'http' is assigned to two launchers."});
  EXPECT_EQ(scheme_map.error_str(), expected_error);
}

}  // namespace
}  // namespace component