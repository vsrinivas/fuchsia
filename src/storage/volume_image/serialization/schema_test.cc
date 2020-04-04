// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "schema.h"

#include <gtest/gtest.h>

#include "rapidjson/schema.h"
#include "src/lib/json_parser/json_parser.h"

namespace storage::volume_image {
namespace {

TEST(SchemaTest, VolumeDescriptorSchemaIsValid) {
  auto schema_json = GetSchema(Schema::kVolumeDescriptor);

  json_parser::JSONParser parser;
  auto document = parser.ParseFromString(schema_json, "volume_descriptor.schema.json");
  ASSERT_FALSE(parser.HasError()) << parser.error_str();
}

}  // namespace
}  // namespace storage::volume_image
