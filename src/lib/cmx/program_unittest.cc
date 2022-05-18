// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/cmx/program.h"

#include <optional>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rapidjson/document.h"
#include "src/lib/json_parser/json_parser.h"

namespace component {
namespace {

class ProgramMetadataTest : public ::testing::Test {
 protected:
  void ExpectFailedParse(const std::string& json, const std::string& expected_error) {
    ProgramMetadata program;
    std::optional parse_error = ParseFrom(&program, json);
    EXPECT_TRUE(program.IsBinaryNull());
    EXPECT_TRUE(program.IsDataNull());
    ASSERT_TRUE(parse_error.has_value());
    EXPECT_THAT(*parse_error, ::testing::HasSubstr(expected_error));
  }

  std::optional<std::string> ParseFrom(ProgramMetadata* program, const std::string& json) {
    json::JSONParser parser;
    rapidjson::Document document = parser.ParseFromString(json, "test_file");
    EXPECT_FALSE(parser.HasError());
    EXPECT_TRUE(program->IsBinaryNull());
    EXPECT_TRUE(program->IsDataNull());
    program->Parse(document, &parser);
    if (parser.HasError()) {
      return parser.error_str();
    }
    return std::nullopt;
  }
};

TEST_F(ProgramMetadataTest, ParseBinary) {
  ProgramMetadata program;
  EXPECT_TRUE(program.IsBinaryNull());
  EXPECT_TRUE(program.IsDataNull());
  std::optional parse_error = ParseFrom(&program, R"JSON({ "binary": "bin/app" })JSON");
  EXPECT_FALSE(parse_error.has_value()) << *parse_error;
  EXPECT_TRUE(program.IsDataNull());
  EXPECT_EQ("bin/app", program.binary());
}

TEST_F(ProgramMetadataTest, ParseBinaryArgs) {
  ProgramMetadata program;
  EXPECT_TRUE(program.IsBinaryNull());
  EXPECT_TRUE(program.IsArgsNull());
  EXPECT_TRUE(program.IsDataNull());
  std::optional parse_error =
      ParseFrom(&program, R"JSON({ "binary": "bin/app", "args": ["-v", "-q"] })JSON");
  EXPECT_FALSE(parse_error.has_value()) << *parse_error;
  EXPECT_FALSE(program.IsBinaryNull());
  EXPECT_FALSE(program.IsArgsNull());
  EXPECT_TRUE(program.IsDataNull());
  EXPECT_EQ("bin/app", program.binary());
  std::vector<std::string> expected_args{"-v", "-q"};
  EXPECT_EQ(expected_args, program.args());
}

TEST_F(ProgramMetadataTest, ParseBinaryArgsWithErrors) {
  ProgramMetadata program;
  std::optional parse_error =
      ParseFrom(&program, R"JSON({ "binary": "bin/app", "args": [0, 1] })JSON");
  ASSERT_TRUE(parse_error.has_value());
  EXPECT_THAT(*parse_error, ::testing::HasSubstr("'args' contains an item that's not a string"));
}

TEST_F(ProgramMetadataTest, ParseBinaryEnvVars) {
  ProgramMetadata program;
  EXPECT_TRUE(program.IsBinaryNull());
  EXPECT_TRUE(program.IsEnvVarsNull());
  EXPECT_TRUE(program.IsDataNull());
  std::optional parse_error =
      ParseFrom(&program, R"JSON({ "binary": "bin/app", "env_vars": ["FOO=1", "BAR=0"] })JSON");
  EXPECT_FALSE(parse_error.has_value()) << *parse_error;
  EXPECT_FALSE(program.IsBinaryNull());
  EXPECT_FALSE(program.IsEnvVarsNull());
  EXPECT_TRUE(program.IsDataNull());
  EXPECT_EQ("bin/app", program.binary());
  std::vector<std::string> expected_env_vars{"FOO=1", "BAR=0"};
  EXPECT_EQ(expected_env_vars, program.env_vars());
}

TEST_F(ProgramMetadataTest, ParseBinaryEnvVarsWithErrors) {
  ProgramMetadata program;
  std::optional parse_error =
      ParseFrom(&program, R"JSON({ "binary": "bin/app", "env_vars": [0, 1] })JSON");
  ASSERT_TRUE(parse_error.has_value());
  EXPECT_THAT(*parse_error,
              ::testing::HasSubstr("'env_vars' contains an item that's not a string"));
}

TEST_F(ProgramMetadataTest, ParseData) {
  ProgramMetadata program;
  EXPECT_TRUE(program.IsBinaryNull());
  EXPECT_TRUE(program.IsDataNull());
  std::optional parse_error = ParseFrom(&program, R"JSON({ "data": "data/component" })JSON");
  EXPECT_FALSE(parse_error.has_value()) << *parse_error;
  EXPECT_FALSE(program.IsDataNull());
  EXPECT_TRUE(program.IsBinaryNull());
  EXPECT_EQ("data/component", program.data());
}

TEST_F(ProgramMetadataTest, ParseDataWithArgs) {
  ProgramMetadata program;
  EXPECT_TRUE(program.IsBinaryNull());
  EXPECT_TRUE(program.IsDataNull());
  std::optional parse_error =
      ParseFrom(&program, R"JSON({ "data": "data/component", "args": ["-v", "-q"] })JSON");
  EXPECT_FALSE(parse_error.has_value()) << *parse_error;
  EXPECT_FALSE(program.IsDataNull());
  EXPECT_TRUE(program.IsBinaryNull());
  EXPECT_EQ("data/component", program.data());
  std::vector<std::string> expected_args{"-v", "-q"};
  EXPECT_EQ(expected_args, program.args());
}

TEST_F(ProgramMetadataTest, ParseBinaryAndData) {
  ProgramMetadata program;
  EXPECT_TRUE(program.IsBinaryNull());
  EXPECT_TRUE(program.IsDataNull());
  std::optional parse_error =
      ParseFrom(&program, R"JSON({ "binary": "bin/app", "data": "data/component" })JSON");
  EXPECT_FALSE(parse_error.has_value()) << *parse_error;
  EXPECT_FALSE(program.IsBinaryNull());
  EXPECT_FALSE(program.IsDataNull());
  EXPECT_EQ("bin/app", program.binary());
  EXPECT_EQ("data/component", program.data());
}

TEST_F(ProgramMetadataTest, ParseUnknownAttributes) {
  ProgramMetadata program;
  EXPECT_TRUE(program.IsDataNull());
  std::optional parse_error =
      ParseFrom(&program, R"JSON({ "data": "data/runner_data", "flabble": "frobble" })JSON");
  EXPECT_FALSE(parse_error.has_value()) << *parse_error;
  EXPECT_FALSE(program.IsDataNull());
  EXPECT_FALSE(program.IsDataNull());
  ProgramMetadata::Attributes expected_attributes = {{"flabble", "frobble"}};
  EXPECT_EQ(program.unknown_attributes(), expected_attributes);
}

TEST_F(ProgramMetadataTest, ParseUnknownAttributesWithNonStringValues) {
  ProgramMetadata program;
  EXPECT_TRUE(program.IsDataNull());
  std::optional parse_error =
      ParseFrom(&program, R"JSON({ "data": "data/runner_data", "number": 4 })JSON");
  ASSERT_TRUE(parse_error.has_value());
  EXPECT_THAT(*parse_error,
              ::testing::HasSubstr("Extra attributes in 'program' must have string values."));
}

TEST_F(ProgramMetadataTest, ParseWithErrors) {
  ExpectFailedParse(R"JSON({})JSON", "Both 'binary' and 'data' in program are missing.");
  ExpectFailedParse(R"JSON({ "binary": 3 })JSON", "'binary' in program is not a string.");
  ExpectFailedParse(R"JSON({ "data": 3 })JSON", "'data' in program is not a string.");
}

}  // namespace
}  // namespace component
