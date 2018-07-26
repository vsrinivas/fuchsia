// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/program_metadata.h"

#include <string>

#include "gtest/gtest.h"

#include "garnet/lib/json/json_parser.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace component {
namespace {

class ProgramMetadataTest : public ::testing::Test {
 protected:
  void ExpectFailedParse(const std::string& json,
                         const std::string& expected_error) {
    std::string error;
    ProgramMetadata program;
    EXPECT_FALSE(ParseFrom(&program, json, &error));
    EXPECT_TRUE(program.IsNull());
    EXPECT_EQ(error, expected_error);
  }

  bool ParseFrom(ProgramMetadata* program, const std::string& json,
                 std::string* error) {
    json::JSONParser parser;
    rapidjson::Document document = parser.ParseFromString(json, "test_file");
    EXPECT_FALSE(parser.HasError());
    EXPECT_TRUE(program->IsNull());
    const bool ret = program->Parse(document, &parser);
    if (parser.HasError()) {
      *error = parser.error_str();
    }
    return ret;
  }
};

TEST_F(ProgramMetadataTest, Parse) {
  ProgramMetadata program;
  EXPECT_TRUE(program.IsNull());
  std::string error;
  EXPECT_TRUE(ParseFrom(&program, R"JSON({ "binary": "bin/app" })JSON",
                        &error));
  EXPECT_FALSE(program.IsNull());
  EXPECT_EQ("bin/app", program.binary());
}

TEST_F(ProgramMetadataTest, ParseWithErrors) {
  ExpectFailedParse(R"JSON({})JSON",
                    "test_file: 'binary' in program is missing.");
  ExpectFailedParse(R"JSON({ "binary": 3 })JSON",
                    "test_file: 'binary' in program is not a string.");
}

}  // namespace
}  // namespace component
