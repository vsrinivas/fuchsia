// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/runtime_metadata.h"

#include <string>

#include "garnet/lib/json/json_parser.h"
#include "gtest/gtest.h"

namespace component {
namespace {

class RuntimeMetadataTest : public ::testing::Test {
 protected:
  void ExpectFailedParse(const std::string& json,
                         const std::string& expected_error) {
    std::string error;
    RuntimeMetadata runtime;
    EXPECT_FALSE(ParseFrom(&runtime, json, &error));
    EXPECT_TRUE(runtime.IsNull());
    EXPECT_EQ(error, expected_error);
  }

  bool ParseFrom(RuntimeMetadata* runtime, const std::string& json,
                 std::string* error) {
    EXPECT_TRUE(runtime->IsNull());
    json::JSONParser parser;
    const bool ret = runtime->ParseFromString(json, "test_file", &parser);
    if (parser.HasError()) {
      *error = parser.error_str();
    }
    return ret;
  }
};

TEST_F(RuntimeMetadataTest, Parse) {
  // empty
  {
    RuntimeMetadata runtime;
    std::string error;
    EXPECT_TRUE(ParseFrom(&runtime, R"JSON({})JSON", &error));
    EXPECT_TRUE(runtime.IsNull());
    EXPECT_EQ(runtime.runner(), "");
  }

  // runner
  {
    RuntimeMetadata runtime;
    std::string error;
    EXPECT_TRUE(ParseFrom(&runtime,
                          R"JSON({ "runner": "dart_runner" })JSON",
                          &error));
    EXPECT_FALSE(runtime.IsNull());
    EXPECT_EQ("dart_runner", runtime.runner());
  }
}

TEST_F(RuntimeMetadataTest, ParseWithErrors) {
  ExpectFailedParse(R"JSON({,,,})JSON",
                    "test_file:1:2: Missing a name for object member.");
  ExpectFailedParse(R"JSON({ "runner": 10 })JSON",
                    "test_file: 'runner' is not a string.");
}

}  // namespace
}  // namespace component
