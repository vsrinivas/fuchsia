// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/cmx/runtime.h"

#include <fcntl.h>
#include <string>

#include "lib/json/json_parser.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"

namespace component {
namespace {

class RuntimeMetadataTest : public ::testing::Test {
 protected:
  void ExpectFailedParse(const std::string& json, std::string expected_error) {
    std::string error;
    RuntimeMetadata runtime;
    std::string json_basename;
    EXPECT_FALSE(ParseFrom(&runtime, json, &error, &json_basename));
    EXPECT_TRUE(runtime.IsNull());
    EXPECT_THAT(error, ::testing::HasSubstr(expected_error));
  }

  bool ParseFrom(RuntimeMetadata* runtime, const std::string& json, std::string* error,
                 std::string* json_basename) {
    EXPECT_TRUE(runtime->IsNull());
    json::JSONParser parser;
    std::string json_path;
    if (!tmp_dir_.NewTempFileWithData(json, &json_path)) {
      return false;
    }
    *json_basename = files::GetBaseName(json_path);
    const int dirfd = open(tmp_dir_.path().c_str(), O_RDONLY);
    const bool ret = runtime->ParseFromFileAt(dirfd, files::GetBaseName(json_path), &parser);
    if (parser.HasError()) {
      *error = parser.error_str();
    }
    return ret;
  }

 private:
  files::ScopedTempDir tmp_dir_;
};

TEST_F(RuntimeMetadataTest, Parse) {
  // empty
  {
    RuntimeMetadata runtime;
    std::string error;
    std::string file_unused;
    EXPECT_TRUE(ParseFrom(&runtime, R"JSON({})JSON", &error, &file_unused));
    EXPECT_TRUE(runtime.IsNull());
    EXPECT_EQ(runtime.runner(), "");
  }

  // runner
  {
    RuntimeMetadata runtime;
    std::string error;
    std::string file_unused;
    EXPECT_TRUE(
        ParseFrom(&runtime, R"JSON({ "runner": "dart_runner" })JSON", &error, &file_unused));
    EXPECT_FALSE(runtime.IsNull());
    EXPECT_EQ("dart_runner", runtime.runner());
  }
}

TEST_F(RuntimeMetadataTest, ParseWithErrors) {
  ExpectFailedParse(R"JSON({,,,})JSON", "Missing a name for object member.");
  ExpectFailedParse(R"JSON({ "runner": 10 })JSON", "'runner' is not a string.");
}

}  // namespace
}  // namespace component
