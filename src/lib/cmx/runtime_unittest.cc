// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/cmx/runtime.h"

#include <fcntl.h>

#include <optional>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/json_parser/json_parser.h"

namespace component {
namespace {

class RuntimeMetadataTest : public ::testing::Test {
 protected:
  void ExpectFailedParse(const std::string& json, std::string expected_error) {
    RuntimeMetadata runtime;
    std::string json_basename;
    std::optional parse_error = ParseFrom(&runtime, json, &json_basename);
    EXPECT_TRUE(runtime.IsNull());
    ASSERT_TRUE(parse_error.has_value());
    EXPECT_THAT(*parse_error, ::testing::HasSubstr(expected_error));
  }

  std::optional<std::string> ParseFrom(RuntimeMetadata* runtime, const std::string& json,
                                       std::string* json_basename) {
    EXPECT_TRUE(runtime->IsNull());
    json::JSONParser parser;
    std::string json_path;
    if (!tmp_dir_.NewTempFileWithData(json, &json_path)) {
      return "could not create temporary file";
    }
    *json_basename = files::GetBaseName(json_path);
    const int dirfd = open(tmp_dir_.path().c_str(), O_RDONLY);
    runtime->ParseFromFileAt(dirfd, files::GetBaseName(json_path), &parser);
    if (parser.HasError()) {
      return parser.error_str();
    }
    return std::nullopt;
  }

 private:
  files::ScopedTempDir tmp_dir_;
};

TEST_F(RuntimeMetadataTest, Parse) {
  // empty
  {
    RuntimeMetadata runtime;
    std::string file_unused;
    std::optional parse_error = ParseFrom(&runtime, R"JSON({})JSON", &file_unused);
    EXPECT_FALSE(parse_error.has_value()) << *parse_error;
    EXPECT_TRUE(runtime.IsNull());
    EXPECT_EQ(runtime.runner(), "");
  }

  // runner
  {
    RuntimeMetadata runtime;
    std::string file_unused;
    std::optional parse_error =
        ParseFrom(&runtime, R"JSON({ "runner": "dart_runner" })JSON", &file_unused);
    EXPECT_FALSE(parse_error.has_value()) << *parse_error;
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
