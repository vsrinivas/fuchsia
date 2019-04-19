// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/cmx/program.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lib/json/json_parser.h"
#include "rapidjson/document.h"

namespace component {
namespace {

class ProgramMetadataTest : public ::testing::Test {
 protected:
  void ExpectFailedParse(const std::string& json,
                         const std::string& expected_error) {
    std::string error;
    ProgramMetadata program;
    EXPECT_FALSE(ParseFrom(&program, json, &error));
    EXPECT_TRUE(program.IsBinaryNull());
    EXPECT_TRUE(program.IsDataNull());
    EXPECT_THAT(error, ::testing::HasSubstr(expected_error));
  }

  bool ParseFrom(ProgramMetadata* program, const std::string& json,
                 std::string* error) {
    json::JSONParser parser;
    rapidjson::Document document = parser.ParseFromString(json, "test_file");
    EXPECT_FALSE(parser.HasError());
    EXPECT_TRUE(program->IsBinaryNull());
    EXPECT_TRUE(program->IsDataNull());
    const bool ret = program->Parse(document, &parser);
    if (parser.HasError()) {
      *error = parser.error_str();
    }
    return ret;
  }
};

TEST_F(ProgramMetadataTest, ParseBinary) {
  ProgramMetadata program;
  EXPECT_TRUE(program.IsBinaryNull());
  EXPECT_TRUE(program.IsDataNull());
  std::string error;
  EXPECT_TRUE(
      ParseFrom(&program, R"JSON({ "binary": "bin/app" })JSON", &error));
  EXPECT_FALSE(program.IsBinaryNull());
  EXPECT_TRUE(program.IsDataNull());
  EXPECT_EQ("bin/app", program.binary());
}

TEST_F(ProgramMetadataTest, ParseBinaryArgs) {
  ProgramMetadata program;
  EXPECT_TRUE(program.IsBinaryNull());
  EXPECT_TRUE(program.IsArgsNull());
  EXPECT_TRUE(program.IsDataNull());
  std::string error;
  EXPECT_TRUE(ParseFrom(
      &program, R"JSON({ "binary": "bin/app", "args": ["-v", "-q"] })JSON",
      &error));
  EXPECT_FALSE(program.IsBinaryNull());
  EXPECT_FALSE(program.IsArgsNull());
  EXPECT_TRUE(program.IsDataNull());
  EXPECT_EQ("bin/app", program.binary());
  std::vector<std::string> expected_args{"-v", "-q"};
  EXPECT_EQ(expected_args, program.args());
}

TEST_F(ProgramMetadataTest, ParseBinaryArgsWithErrors) {
  std::string error;
  ProgramMetadata program;
  EXPECT_FALSE(ParseFrom(
      &program, R"JSON({ "binary": "bin/app", "args": [0, 1] })JSON", &error));
  EXPECT_THAT(error,
              ::testing::HasSubstr(
                  "'args' in program contains an item that's not a string"));
}

TEST_F(ProgramMetadataTest, ParseData) {
  ProgramMetadata program;
  EXPECT_TRUE(program.IsBinaryNull());
  EXPECT_TRUE(program.IsDataNull());
  std::string error;
  EXPECT_TRUE(
      ParseFrom(&program, R"JSON({ "data": "data/component" })JSON", &error));
  EXPECT_FALSE(program.IsDataNull());
  EXPECT_TRUE(program.IsBinaryNull());
  EXPECT_EQ("data/component", program.data());
}

TEST_F(ProgramMetadataTest, ParseBinaryAndData) {
  ProgramMetadata program;
  EXPECT_TRUE(program.IsBinaryNull());
  EXPECT_TRUE(program.IsDataNull());
  std::string error;
  EXPECT_TRUE(ParseFrom(
      &program, R"JSON({ "binary": "bin/app", "data": "data/component" })JSON",
      &error));
  EXPECT_FALSE(program.IsBinaryNull());
  EXPECT_FALSE(program.IsDataNull());
  EXPECT_EQ("bin/app", program.binary());
  EXPECT_EQ("data/component", program.data());
}

TEST_F(ProgramMetadataTest, ParseWithErrors) {
  ExpectFailedParse(R"JSON({})JSON",
                    "Both 'binary' and 'data' in program are missing.");
  ExpectFailedParse(R"JSON({ "binary": 3 })JSON",
                    "'binary' in program is not a string.");
  ExpectFailedParse(R"JSON({ "data": 3 })JSON",
                    "'data' in program is not a string.");
}

}  // namespace
}  // namespace component
