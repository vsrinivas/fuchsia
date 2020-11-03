// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "schema.h"

#include <gtest/gtest.h>

#include "rapidjson/schema.h"
#include "src/lib/json_parser/json_parser.h"
#include "src/lib/json_parser/rapidjson_validation.h"

namespace storage::volume_image {
namespace {

TEST(SchemaTest, VolumeDescriptorSchemaIsValid) {
  auto schema_json = GetSchema(Schema::kVolumeDescriptor);

  json_parser::JSONParser parser;
  auto document = parser.ParseFromString(schema_json, "volume_descriptor.schema.json");
  ASSERT_FALSE(parser.HasError()) << parser.error_str();
  auto unique_schema_result = json_parser::InitSchema(schema_json);
  ASSERT_TRUE(unique_schema_result.is_ok()) << unique_schema_result.error_value().ToString();
}

TEST(SchemaTest, AddressDescriptorSchemaIsValid) {
  auto schema_json = GetSchema(Schema::kAddressDescriptor);

  json_parser::JSONParser parser;
  auto document = parser.ParseFromString(schema_json, "address_descriptor.schema.json");
  ASSERT_FALSE(parser.HasError()) << parser.error_str();
  auto unique_schema_result = json_parser::InitSchema(schema_json);
  ASSERT_TRUE(unique_schema_result.is_ok()) << unique_schema_result.error_value().ToString();
}

TEST(SchemaTest, VolumeImageSchemaIsValid) {
  auto schema_json = GetSchema(Schema::kVolumeImage);
  json_parser::JSONParser parser;
  auto document = parser.ParseFromString(schema_json, "volume_image.schema.json");
  ASSERT_FALSE(parser.HasError()) << parser.error_str();
  auto unique_schema_result = json_parser::InitSchema(schema_json);
  ASSERT_TRUE(unique_schema_result.is_ok()) << unique_schema_result.error_value().ToString();
}

TEST(SchemaTest, BlobManifestSchemaIsValid) {
  auto schema_json = GetSchema(Schema::kVolumeImage);
  json_parser::JSONParser parser;
  auto document = parser.ParseFromString(schema_json, "blob_manifest.schema.json");
  ASSERT_FALSE(parser.HasError()) << parser.error_str();
  auto unique_schema_result = json_parser::InitSchema(schema_json);
  ASSERT_TRUE(unique_schema_result.is_ok()) << unique_schema_result.error_value().ToString();
}

}  // namespace
}  // namespace storage::volume_image
