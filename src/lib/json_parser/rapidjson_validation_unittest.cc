// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/json_parser/rapidjson_validation.h"

#include <gtest/gtest.h>

namespace json_parser {

constexpr fxl::StringView kInvalidSchema = "Hello";

constexpr fxl::StringView kValidSchema = R"({
  "type": "object",
  "additionalProperties": true,
  "properties": {
    "foo": {
      "type": "string"
    }
  },
  "required": ["foo"]
})";

constexpr fxl::StringView kInvalidJson = R"({
  "hello": "world"
})";

constexpr fxl::StringView kValidJson = R"({
  "foo": "bar",
  "hello": "world"
})";

bool ParseJson(fxl::StringView json, rapidjson::Document* document) {
  document->Parse(json.data(), json.size());
  return !document->HasParseError();
}

TEST(RapidJsonValidation, InvalidSchema) { EXPECT_FALSE(InitSchemaDeprecated(kInvalidSchema)); }

TEST(RapidJsonValidation, ValidSchema) { EXPECT_TRUE(InitSchemaDeprecated(kValidSchema)); }

TEST(RapidJsonValidation, ValidJson) {
  auto schema = InitSchemaDeprecated(kValidSchema);
  ASSERT_TRUE(schema);

  rapidjson::Document document;
  ASSERT_TRUE(ParseJson(kValidJson, &document));

  EXPECT_TRUE(ValidateSchemaDeprecated(document, *schema));
}

TEST(RapidJsonValidation, InvalidJson) {
  auto schema = InitSchemaDeprecated(kValidSchema);
  ASSERT_TRUE(schema);

  rapidjson::Document document;
  ASSERT_TRUE(ParseJson(kInvalidJson, &document));

  EXPECT_FALSE(ValidateSchemaDeprecated(document, *schema));
}

}  // namespace json_parser
