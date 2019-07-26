// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/public/lib/rapidjson_utils/rapidjson_validation.h"

#include "gtest/gtest.h"

namespace rapidjson_utils {

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

TEST(RapidJsonValidation, InvalidSchema) { EXPECT_FALSE(InitSchema(kInvalidSchema)); }

TEST(RapidJsonValidation, ValidSchema) { EXPECT_TRUE(InitSchema(kValidSchema)); }

TEST(RapidJsonValidation, ValidJson) {
  auto schema = InitSchema(kValidSchema);
  ASSERT_TRUE(schema);

  rapidjson::Document document;
  ASSERT_TRUE(ParseJson(kValidJson, &document));

  EXPECT_TRUE(ValidateSchema(document, *schema));
}

TEST(RapidJsonValidation, InvalidJson) {
  auto schema = InitSchema(kValidSchema);
  ASSERT_TRUE(schema);

  rapidjson::Document document;
  ASSERT_TRUE(ParseJson(kInvalidJson, &document));

  EXPECT_FALSE(ValidateSchema(document, *schema));
}

}  // namespace rapidjson_utils
