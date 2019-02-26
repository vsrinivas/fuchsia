// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/firebase_auth/testing/json_schema.h"

#include "gtest/gtest.h"

namespace json_schema {
namespace {

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

TEST(JsonSchemaTest, InvalidSchema) {
  EXPECT_FALSE(InitSchema(kInvalidSchema));
}

TEST(JsonSchemaTest, ValidSchema) { EXPECT_TRUE(InitSchema(kValidSchema)); }

TEST(JsonSchemaTest, ValidJson) {
  auto schema = InitSchema(kValidSchema);
  ASSERT_TRUE(schema);

  rapidjson::Document document;
  ASSERT_TRUE(ParseJson(kValidJson, &document));

  EXPECT_TRUE(ValidateSchema(document, *schema));
}

TEST(JsonSchemaTest, InvalidJson) {
  auto schema = InitSchema(kValidSchema);
  ASSERT_TRUE(schema);

  rapidjson::Document document;
  ASSERT_TRUE(ParseJson(kInvalidJson, &document));

  EXPECT_FALSE(ValidateSchema(document, *schema));
}

}  // namespace
}  // namespace json_schema
