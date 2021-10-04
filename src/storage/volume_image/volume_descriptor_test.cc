// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/volume_descriptor.h"

#include <lib/fit/function.h>
#include <zircon/assert.h>

#include <limits>
#include <string_view>
#include <type_traits>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "rapidjson/document.h"
#include "rapidjson/schema.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "src/lib/json_parser/json_parser.h"
#include "src/lib/json_parser/rapidjson_validation.h"
#include "src/storage/volume_image/options.h"
#include "src/storage/volume_image/serialization/schema.h"

namespace storage::volume_image {
namespace {

TEST(VolumeDescriptorTest, SerializeReturnsSchemaValidData) {
  VolumeDescriptor descriptor = {};
  descriptor.options = {Option::kNone};
  descriptor.encryption = EncryptionType::kZxcrypt;
  auto schema_json = GetSchema(Schema::kVolumeDescriptor);
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
  auto validation_result = json_parser::ValidateSchema(document, schema_result.value(),
                                                       "VolumeDescriptor::Serialize output");
  EXPECT_TRUE(validation_result.is_ok()) << validation_result.error_value();
}

std::string GetSerializedJson(fit::function<void(rapidjson::Document*)> mutator = nullptr) {
  // A Valid JSON being serialized.
  static constexpr std::string_view kSerializedVolumeDescriptor = R"(
    {
      "magic": 11602964,
      "instance_guid": "04030201-0605-0807-1009-111213141516",
      "type_guid": "A4A3A2A1-B6B5-C8C7-D0D1-E0E1E2E3E4E5",
      "name": "i-have-a-name",
      "block_size": 512,
      "encryption_type": "ENCRYPTION_TYPE_ZXCRYPT",
      "options" : [
        "OPTION_NONE",
        "OPTION_EMPTY"
      ]
    }
  )";
  json_parser::JSONParser parser;
  rapidjson::Document parsed_document = parser.ParseFromString(kSerializedVolumeDescriptor.data(),
                                                               "serialized_volume_descriptor.json");
  ZX_ASSERT_MSG(!parser.HasError(), "%s\n", parser.error_str().c_str());
  if (mutator != nullptr) {
    mutator(&parsed_document);
  }
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  parsed_document.Accept(writer);
  return buffer.GetString();
}

TEST(VolumeDescriptorTest, DeserializeSerializedDataIsOk) {
  const auto deserialized_result = VolumeDescriptor::Deserialize(GetSerializedJson());
  ASSERT_TRUE(deserialized_result.is_ok());

  const auto serialized_result = deserialized_result.value().Serialize();
  ASSERT_TRUE(serialized_result.is_ok());

  const auto redeserialized_result = VolumeDescriptor::Deserialize(serialized_result.value());
  ASSERT_TRUE(redeserialized_result.is_ok());

  const auto& deserialized_1 = deserialized_result.value();
  const auto& deserialized_2 = deserialized_result.value();

  ASSERT_EQ(deserialized_1.type, deserialized_2.type);
  ASSERT_EQ(deserialized_1.block_size, deserialized_2.block_size);
  ASSERT_EQ(deserialized_1.instance, deserialized_2.instance);
  ASSERT_EQ(deserialized_1.name, deserialized_2.name);
  ASSERT_EQ(deserialized_1.encryption, deserialized_2.encryption);
  ASSERT_EQ(deserialized_1.options, deserialized_2.options);
}

TEST(VolumeDescriptorTest, DeserializeFromValidDataReturnsVolumeDescriptor) {
  constexpr std::string_view kTypeGuid = "A4A3A2A1-B6B5-C8C7-D0D1-E0E1E2E3E4E5";
  constexpr std::string_view kInstanceGuid = "04030201-0605-0807-1009-111213141516";
  constexpr std::string_view kName = "i-have-a-name";
  const std::string kSerializedVolumeDescriptor = GetSerializedJson();

  auto descriptor_result = VolumeDescriptor::Deserialize(kSerializedVolumeDescriptor);
  ASSERT_TRUE(descriptor_result.is_ok()) << descriptor_result.take_error();
  auto descriptor = descriptor_result.take_value();
  auto expected_type_guid = Guid::FromString(kTypeGuid);
  auto expected_instance_guid = Guid::FromString(kInstanceGuid);
  ASSERT_TRUE(expected_type_guid.is_ok());
  ASSERT_TRUE(expected_instance_guid.is_ok());

  EXPECT_EQ(expected_type_guid.value(), descriptor.type);
  EXPECT_EQ(expected_instance_guid.value(), descriptor.instance);
  EXPECT_EQ(kName, descriptor.name);
  EXPECT_EQ(512u, descriptor.block_size);
  EXPECT_EQ(EncryptionType::kZxcrypt, descriptor.encryption);
  EXPECT_THAT(descriptor.options, ::testing::UnorderedElementsAre(Option::kNone, Option::kEmpty));
}

TEST(VolumeDescriptorTest, DeserializeWithBadTypeGuidIsError) {
  ASSERT_TRUE(VolumeDescriptor::Deserialize(GetSerializedJson([](auto* document) {
                (*document)["type_guid"] = "012345678";
              })).is_error());
}

TEST(VolumeDescriptorTest, DeserializeWithBadInstanceGuidIsError) {
  ASSERT_TRUE(VolumeDescriptor::Deserialize(GetSerializedJson([](auto* document) {
                (*document)["instance_guid"] = "012345678";
              })).is_error());
}

TEST(VolumeDescriptorTest, DeserializeWithLongNameIsError) {
  constexpr std::string_view kName = "01234567890123456789012345678901234567891";
  auto descriptor_result = VolumeDescriptor::Deserialize(GetSerializedJson(
      [kName](auto* document) { (*document)["name"] = rapidjson::StringRef(kName.data()); }));
  ASSERT_FALSE(descriptor_result.is_ok());
}

TEST(VolumeDescriptorTest, DeserializeWithBadMagicIsError) {
  ASSERT_TRUE(VolumeDescriptor::Deserialize(GetSerializedJson([](auto* document) {
                (*document)["magic"] = 0xB201C4;
              })).is_error());
}

TEST(VolumeDescriptorTest, DeserializeWithBadEncryptionTypeIsError) {
  ASSERT_TRUE(VolumeDescriptor::Deserialize(GetSerializedJson([](auto* document) {
                (*document)["encryption_type"] = "BAD_OR_UNKNOWN_ENCRYPTION";
              })).is_error());
}

TEST(VolumeDescriptorTest, DeserializeWithBadOptionIsError) {
  ASSERT_TRUE(VolumeDescriptor::Deserialize(GetSerializedJson([](auto* document) {
                (*document)["options"].GetArray().PushBack("BAD_OR_UNKNOWN_OPTION",
                                                           document->GetAllocator());
              })).is_error());
}

}  // namespace
}  // namespace storage::volume_image
