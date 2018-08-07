// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/json/json_parser.h"

#include <fcntl.h>
#include <stdio.h>
#include <functional>
#include <string>

#include "gtest/gtest.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/files/path.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/strings/concatenate.h"
#include "lib/fxl/strings/substitute.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace json {
namespace {

class JSONParserTest : public ::testing::Test {
 protected:
  // ExpectFailedParse() will replace '$0' with the JSON filename, if present.
  void ExpectFailedParse(JSONParser* parser, const std::string& json,
                         std::string expected_error) {
    props_found_ = 0;
    const std::string json_file = NewJSONFile(json);
    std::string error;
    EXPECT_FALSE(ParseFromFile(parser, json_file, &error));
    EXPECT_EQ(error, fxl::Substitute(expected_error, json_file));
    EXPECT_EQ(0, props_found_);
  }

  bool ParseFromFile(JSONParser* parser, const std::string& file,
                     std::string* error) {
    rapidjson::Document document = parser->ParseFromFile(file);
    if (!parser->HasError()) {
      InterpretDocument(parser, std::move(document));
    }
    *error = parser->error_str();
    return !parser->HasError();
  }

  bool ParseFromFileAt(JSONParser* parser, int dirfd, const std::string& file,
                       std::string* error) {
    rapidjson::Document document = parser->ParseFromFileAt(dirfd, file);
    if (!parser->HasError()) {
      InterpretDocument(parser, std::move(document));
    }
    *error = parser->error_str();
    return !parser->HasError();
  }

  bool ParseFromDirectory(JSONParser* parser, const std::string& dir,
                          std::string* error) {
    std::function<void(rapidjson::Document)> cb =
        std::bind(&JSONParserTest::InterpretDocument, this, parser,
                  std::placeholders::_1);
    parser->ParseFromDirectory(dir, cb);
    *error = parser->error_str();
    return !parser->HasError();
  }

  std::string NewJSONFile(const std::string& json) {
    std::string json_file;
    if (!tmp_dir_.NewTempFileWithData(json, &json_file)) {
      return "";
    }
    return json_file;
  }

  std::string NewJSONFileInDir(const std::string& dir,
                               const std::string& json) {
    const std::string json_file =
        fxl::Concatenate({dir, "/json_file", std::to_string(unique_id_++)});
    if (!files::WriteFile(json_file, json.data(), json.size())) {
      return "";
    }
    return json_file;
  }

  void InterpretDocument(JSONParser* parser, rapidjson::Document document) {
    if (!document.IsObject()) {
      parser->ReportError("Document is not an object.");
      return;
    }

    auto prop1 = document.FindMember("prop1");
    if (prop1 == document.MemberEnd()) {
      // Allow missing.
    } else if (!prop1->value.IsString()) {
      parser->ReportError("prop1 has wrong type");
    } else {
      ++props_found_;
    }

    auto prop2 = document.FindMember("prop2");
    if (prop2 == document.MemberEnd()) {
      // Allow missing.
    } else if (!prop2->value.IsInt()) {
      parser->ReportError("prop2 has wrong type");
    } else {
      ++props_found_;
    }
  }

  files::ScopedTempDir tmp_dir_;
  int props_found_ = 0;

 private:
  int unique_id_ = 1;
};

TEST_F(JSONParserTest, ReadInvalidFile) {
  const std::string invalid_path =
      fxl::Substitute("$0/does_not_exist", tmp_dir_.path());
  std::string error;
  JSONParser parser;
  EXPECT_FALSE(ParseFromFile(&parser, invalid_path, &error));
  EXPECT_EQ(error, fxl::Substitute("Failed to read file: $0", invalid_path));
}

TEST_F(JSONParserTest, ParseWithErrors) {
  std::string json;

  // One error, in parsing.
  {
    const std::string json = R"JSON({
  "prop1": "missing closing quote,
  "prop2": 42
  })JSON";
    JSONParser parser;
    ExpectFailedParse(&parser, json, "$0:2:35: Invalid encoding in string.");
  }

  // Multiple errors, after parsing.
  {
    const std::string json = R"JSON({
  "prop1": 42,
  "prop2": "wrong_type"
  })JSON";
    JSONParser parser;
    ExpectFailedParse(&parser, json,
                      "$0: prop1 has wrong type\n$0: prop2 has wrong type");
  }
}

TEST_F(JSONParserTest, ParseFromString) {
  const std::string json = R"JSON({
  "prop1": "missing closing quote
  })JSON";
  JSONParser parser;
  parser.ParseFromString(json, "test_file");
  EXPECT_TRUE(parser.HasError());
  EXPECT_EQ(parser.error_str(), "test_file:2:34: Invalid encoding in string.");
}

TEST_F(JSONParserTest, ParseTwice) {
  std::string json;
  JSONParser parser;

  // Two failed parses. Errors should accumulate.
  json = R"JSON({
  "prop1": invalid_value,
  })JSON";
  parser.ParseFromString(json, "test_file");

  json = R"JSON({
  "prop1": "missing closing quote
  })JSON";
  parser.ParseFromString(json, "test_file");

  EXPECT_TRUE(parser.HasError());
  EXPECT_EQ(parser.error_str(),
            "test_file:2:12: Invalid value.\n"
            "test_file:2:34: Invalid encoding in string.");
  EXPECT_EQ(0, props_found_);
}

TEST_F(JSONParserTest, ParseValid) {
  const std::string json = R"JSON({
  "prop1": "foo",
  "prop2": 42
  })JSON";
  const std::string file = NewJSONFile(json);
  std::string error;
  JSONParser parser;
  EXPECT_TRUE(ParseFromFile(&parser, file, &error));
  EXPECT_EQ("", error);
  EXPECT_EQ(2, props_found_);
}

TEST_F(JSONParserTest, ParseFromFileAt) {
  const std::string json = R"JSON({
  "prop1": "foo",
  "prop2": 42
  })JSON";
  const std::string file = NewJSONFile(json);
  const std::string basename = files::GetBaseName(file);
  const int dirfd = open(tmp_dir_.path().c_str(), O_RDONLY);
  ASSERT_GT(dirfd, 0);

  std::string error;
  JSONParser parser;
  EXPECT_TRUE(ParseFromFileAt(&parser, dirfd, basename, &error));
  EXPECT_EQ("", error);
  EXPECT_EQ(2, props_found_);
}

TEST_F(JSONParserTest, ParseFromDirectory) {
  const std::string json1 = R"JSON({
  "prop1": "foo"
  })JSON";
  const std::string json2 = R"JSON({
  "prop2": 42
  })JSON";
  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  NewJSONFileInDir(dir, json1);
  NewJSONFileInDir(dir, json2);

  std::string error;
  JSONParser parser;
  EXPECT_TRUE(ParseFromDirectory(&parser, dir, &error));
  EXPECT_EQ("", error);
  EXPECT_EQ(2, props_found_);
}

TEST_F(JSONParserTest, ParseFromDirectoryWithErrors) {
  const std::string json1 = R"JSON({,,,})JSON";
  const std::string json2 = R"JSON({
  "prop2": 42
  })JSON";
  std::string dir;
  ASSERT_TRUE(tmp_dir_.NewTempDir(&dir));
  const std::string json_file1 = NewJSONFileInDir(dir, json1);
  NewJSONFileInDir(dir, json2);

  // Parsing should continue even when one file fails to parse.
  std::string error;
  JSONParser parser;
  EXPECT_FALSE(ParseFromDirectory(&parser, dir, &error));
  EXPECT_EQ(error,
            fxl::Concatenate(
                {json_file1, ":1:2: Missing a name for object member."}));
  EXPECT_EQ(1, props_found_);
}

}  // namespace
}  // namespace json
