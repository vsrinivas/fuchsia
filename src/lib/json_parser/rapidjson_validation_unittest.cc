// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/json_parser/rapidjson_validation.h"

#include <gtest/gtest.h>

namespace json_parser {

constexpr std::string_view kInvalidSchemaJson1 = "Hello";

constexpr std::string_view kInvalidSchemaJson2 = R"({
  "hello": "world",
})";

constexpr std::string_view kValidSchema = R"({
  "type": "object",
  "additionalProperties": true,
  "properties": {
    "foo": {
      "type": "string"
    }
  },
  "required": ["foo"]
})";

constexpr std::string_view kInvalidJson = R"({
  "hello": "world"
})";

constexpr std::string_view kValidJson = R"({
  "foo": "bar",
  "hello": "world"
})";

bool ParseJson(std::string_view json, rapidjson::Document* document) {
  document->Parse(json.data(), json.size());
  return !document->HasParseError();
}

TEST(RapidJsonValidation, InvalidSchemaJson) {
  {
    auto result = InitSchema(kInvalidSchemaJson1);
    ASSERT_FALSE(result.is_ok());
    auto& err = result.error_value();
    EXPECT_EQ(err.offset, 0u);
    EXPECT_EQ(err.code, rapidjson::kParseErrorValueInvalid);
  }
  {
    auto result = InitSchema(kInvalidSchemaJson2);
    ASSERT_FALSE(result.is_ok());
    auto& err = result.error_value();
    EXPECT_EQ(err.offset, 22u);
    EXPECT_EQ(err.code, rapidjson::kParseErrorObjectMissName);
  }
}

TEST(RapidJsonValidation, ValidSchema) {
  auto result = InitSchema(kValidSchema);
  EXPECT_TRUE(result.is_ok()) << result.error_value().ToString();
}

TEST(RapidJsonValidation, ValidJson) {
  auto result = InitSchema(kValidSchema);
  ASSERT_TRUE(result.is_ok()) << result.error_value().ToString();
  auto& schema = result.value();

  rapidjson::Document document;
  ASSERT_TRUE(ParseJson(kValidJson, &document));

  auto schema_result = ValidateSchema(document, schema);
  EXPECT_TRUE(schema_result.is_ok()) << schema_result.error_value();
}

TEST(RapidJsonValidation, InvalidJson) {
  auto result = InitSchema(kValidSchema);
  ASSERT_TRUE(result.is_ok()) << result.error_value().ToString();
  auto& schema = result.value();

  rapidjson::Document document;
  ASSERT_TRUE(ParseJson(kInvalidJson, &document));

  auto schema_result = ValidateSchema(document, schema);
  EXPECT_FALSE(schema_result.is_ok());
}

}  // namespace json_parser
