// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/address_descriptor.h"

#include <zircon/assert.h>

#include <string>
#include <string_view>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rapidjson/document.h"
#include "rapidjson/schema.h"
#include "rapidjson/writer.h"
#include "src/lib/json_parser/json_parser.h"
#include "src/lib/json_parser/rapidjson_validation.h"
#include "src/storage/volume_image/serialization/schema.h"

namespace storage::volume_image {
namespace {

std::string GetSerializedJson(fit::function<void(rapidjson::Document*)> mutator = nullptr) {
  constexpr std::string_view kSerializedAddressDescriptor = R"(
    {
        "magic": 12526821592682033285,
        "mappings": [
          {
            "source": 20,
            "target": 120,
            "count": 10,
            "options": {}
          },
          {
            "source": 250,
            "target": 160,
            "count": 10
          },
          {
            "source": 2900,
            "target": 170,
            "count": 10,
            "size": 20
          }
        ]
    })";
  json_parser::JSONParser parser;
  rapidjson::Document parsed_document = parser.ParseFromString(
      kSerializedAddressDescriptor.data(), "serialized_address_descriptor.json");
  ZX_ASSERT_MSG(!parser.HasError(), "%s\n", parser.error_str().c_str());
  if (mutator != nullptr) {
    mutator(&parsed_document);
  }
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  parsed_document.Accept(writer);
  return buffer.GetString();
}

TEST(AddressDescriptorTest, SerializeReturnsSchemaValidData) {
  AddressDescriptor descriptor = {};
  descriptor.mappings = std::vector<AddressMap>({
      {.source = 10,
       .target = 20,
       .count = 10,
       .options = {{"random_option_1", 32}, {"random_option_2", 33}}},
      {.source = 20, .target = 30, .count = 10},
  });
  auto schema_json = GetSchema(Schema::kAddressDescriptor);
  ASSERT_FALSE(schema_json.empty());

  json_parser::JSONParser parser;
  auto result = descriptor.Serialize();
  ASSERT_TRUE(result.is_ok()) << result.error();
  auto value = result.take_value();
  auto document =
      parser.ParseFromString(std::string(value.begin(), value.end()), "serialized.json");
  ASSERT_FALSE(parser.HasError()) << parser.error_str();
  auto schema_result = json_parser::InitSchema(schema_json);
  ASSERT_TRUE(schema_result.is_ok()) << schema_result.error_value().ToString();
  auto validation_result = json_parser::ValidateSchema(document, schema_result.value());
  EXPECT_TRUE(validation_result.is_ok()) << validation_result.error_value();
}

MATCHER(AddressMapEq, "") {
  auto [a, b] = arg;
  return a.source == b.source && a.target == b.target && a.count == b.count && a.size == b.size;
}

TEST(AddressDescriptorTest, DeserializeSerializedDataIsOk) {
  const auto deserialized_result = AddressDescriptor::Deserialize(GetSerializedJson());
  ASSERT_TRUE(deserialized_result.is_ok());

  const auto serialized_result = deserialized_result.value().Serialize();
  ASSERT_TRUE(serialized_result.is_ok());

  const auto redeserialized_result = AddressDescriptor::Deserialize(serialized_result.value());
  ASSERT_TRUE(redeserialized_result.is_ok());

  const auto& deserialized_1 = deserialized_result.value();
  const auto& deserialized_2 = deserialized_result.value();

  ASSERT_THAT(deserialized_1.mappings,
              testing::UnorderedPointwise(AddressMapEq(), deserialized_2.mappings));
}

TEST(AddressDescriptorTest, DeserializeFromValidDataReturnsAddressDescritptor) {
  const auto deserialized_result = AddressDescriptor::Deserialize(GetSerializedJson());
  ASSERT_TRUE(deserialized_result.is_ok()) << deserialized_result.error();

  ASSERT_THAT(
      deserialized_result.value().mappings,
      testing::UnorderedPointwise(
          AddressMapEq(), std::vector<AddressMap>({
                              {.source = 250, .target = 160, .count = 10, .size = std::nullopt},
                              {.source = 20, .target = 120, .count = 10, .size = std::nullopt},
                              {.source = 2900, .target = 170, .count = 10, .size = 20},
                          })));
}

TEST(AddressMapTest, DebugStringIsOk) {
  AddressMap map = {
      .source = 100,
      .target = 200,
      .count = 50,
  };

  EXPECT_THAT(map.DebugString(),
              testing::AllOf(testing::ContainsRegex("source:[ ]+100"),
                             testing::ContainsRegex("target:[ ]+200"),
                             testing::ContainsRegex("count:[ ]+50"),
                             testing::ContainsRegex("size:[ ]+std::nullopt"),
                             testing::ContainsRegex("options:[ ]+\\{[ \n]*\\}")));

  map.size = 150;
  EXPECT_THAT(
      map.DebugString(),
      testing::AllOf(testing::ContainsRegex("source:[ ]+100"),
                     testing::ContainsRegex("target:[ ]+200"),
                     testing::ContainsRegex("count:[ ]+50"), testing::ContainsRegex("size:[ ]+150"),
                     testing::ContainsRegex("options:[ ]+\\{[ \n]*\\}")));

  map.options["option_name"] = 1234;
  map.options["option_name_2"] = 12345;
  EXPECT_THAT(
      map.DebugString(),
      testing::AllOf(
          testing::ContainsRegex("source:[ ]+100"), testing::ContainsRegex("target:[ ]+200"),
          testing::ContainsRegex("count:[ ]+50"), testing::ContainsRegex("size:[ ]+150"),
          testing::ContainsRegex(
              "options:[ ]+\\{\n[ ]+option_name:[ ]+1234[ \n]+option_name_2:[ ]+12345[ \n]+\\}")));
}

TEST(AddressDescriptorTest, DeserializeWithBadMagicIsError) {
  ASSERT_TRUE(AddressDescriptor::Deserialize(GetSerializedJson([](auto* document) {
                (*document)["magic"] = AddressDescriptor::kMagic - 1;
              })).is_error());
}

TEST(AddressDescriptorTest, DeserializeWithEmptyMappingsIsError) {
  ASSERT_TRUE(AddressDescriptor::Deserialize(GetSerializedJson([](auto* document) {
                rapidjson::Value value;
                value.SetArray();
                (*document)["mappings"] = value;
              })).is_error());
}

}  // namespace

}  // namespace storage::volume_image
