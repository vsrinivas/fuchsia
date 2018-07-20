// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/json/json_parser.h"

#include <stdio.h>
#include <string>

#include "gtest/gtest.h"
#include "lib/fxl/files/path.h"
#include "lib/fxl/files/scoped_temp_dir.h"
#include "lib/fxl/strings/concatenate.h"
#include "lib/fxl/strings/string_printf.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace json {
namespace {

class JSONParserTest : public ::testing::Test {
 protected:
  // ExpectFailedParse() will replace '$0' with the JSON filename, if present.
  void ExpectFailedParse(const std::string& json, std::string expected_error) {
    const std::string json_file = NewJSONFile(json);
    std::string error;
    EXPECT_FALSE(Parse(json_file, &error));
    std::string resolved_expected_error;
    // TODO(DX-338): Use strings/substitute.h once that actually exists in fxl.
    size_t pos;
    while ((pos = expected_error.find("$0")) != std::string::npos) {
      expected_error.replace(pos, 2, json_file);
    }
    EXPECT_EQ(error, expected_error);
  }

  bool Parse(const std::string& file, std::string* error) {
    JSONParser parser;
    rapidjson::Document document = parser.ParseFrom(file);
    if (!parser.HasError()) {
      InterpretDocument(&parser, document);
    }
    *error = parser.error_str();
    return !parser.HasError();
  }

  std::string NewJSONFile(const std::string& json) {
    std::string json_file;
    if (!tmp_dir_.NewTempFile(&json_file)) {
      return "";
    }

    FILE* tmpf = fopen(json_file.c_str(), "w");
    fprintf(tmpf, "%s", json.c_str());
    fclose(tmpf);
    return json_file;
  }

  void InterpretDocument(JSONParser* parser,
                         const rapidjson::Document& document) {
    if (!document.IsObject()) {
      parser->ReportError("Document is not an object.");
      return;
    }

    auto prop1 = document.FindMember("prop1");
    if (prop1 == document.MemberEnd()) {
      parser->ReportError("missing prop1");
    } else if (!prop1->value.IsString()) {
      parser->ReportError("prop1 has wrong type");
    }

    auto prop2 = document.FindMember("prop2");
    if (prop2 == document.MemberEnd()) {
      parser->ReportError("missing prop2");
    } else if (!prop2->value.IsInt()) {
      parser->ReportError("prop2 has wrong type");
    }
  }

  files::ScopedTempDir tmp_dir_;
};

TEST_F(JSONParserTest, ReadInvalid) {
  const std::string invalid_path =
      fxl::StringPrintf("%s/does_not_exist", tmp_dir_.path().c_str());
  std::string error;
  EXPECT_FALSE(Parse(invalid_path, &error));
  EXPECT_EQ(error,
            fxl::StringPrintf("Failed to read file: %s", invalid_path.c_str()));
}

TEST_F(JSONParserTest, ParseWithErrors) {
  std::string json;

  // One error, in parsing.
  json = R"JSON({
  "prop1": "missing closing quote,
  "prop2": 42
  })JSON";
  ExpectFailedParse(json, "$0:2:35: Invalid encoding in string.");

  // Multiple errors, after parsing.
  json = R"JSON({
  "prop2": "wrong_type"
  })JSON";
  ExpectFailedParse(json, "$0: missing prop1\n$0: prop2 has wrong type");
}

TEST_F(JSONParserTest, Parse) {
  const std::string json = R"JSON({
  "prop1": "foo",
  "prop2": 42
  })JSON";
  const std::string file = NewJSONFile(json);
  std::string error;
  EXPECT_TRUE(Parse(file, &error));
  EXPECT_EQ("", error);
}

}  // namespace
}  // namespace json
