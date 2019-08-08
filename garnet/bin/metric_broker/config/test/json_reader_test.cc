// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/metric_broker/config/json_reader.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/schema.h"
#include "rapidjson/stringbuffer.h"

namespace broker_service {
namespace {

constexpr std::string_view kSchemaPath = "pkg/data/testdata/fake.schema.json";

constexpr std::string_view kValidSchema = R"(
    {
        "required_field": false,
        "optional_field": false
    }
)";

constexpr std::string_view kInvalidSchema = R"(
    {
        "optional_field": false,
    }
)";

class JsonReaderTest : public ::testing::Test {
 public:
  static void SetUpTestSuite() {
    std::ifstream config_stream(kSchemaPath.data());
    ASSERT_TRUE(config_stream.good()) << strerror(errno);
    rapidjson::IStreamWrapper config_wrapper(config_stream);
    rapidjson::Document schema_doc;
    ASSERT_FALSE(schema_doc.ParseStream(config_wrapper).HasParseError());
    schema_doc_ = std::make_unique<rapidjson::SchemaDocument>(schema_doc);
  }

  static void TearDownTestSuite() { schema_doc_.reset(); }

  static rapidjson::SchemaDocument* GetSchema() { return schema_doc_.get(); }

 private:
  static std::unique_ptr<rapidjson::SchemaDocument> schema_doc_;
};

std::unique_ptr<rapidjson::SchemaDocument> JsonReaderTest::schema_doc_ = nullptr;

TEST_F(JsonReaderTest, IsOkIsTrueForSchemaCompilantJson) {
  rapidjson::Document config;
  config.Parse(kValidSchema.data(), kValidSchema.size());
  JsonReader reader(std::move(config), GetSchema());

  ASSERT_TRUE(reader.Validate());
  ASSERT_TRUE(reader.IsOk());
  ASSERT_TRUE(reader.error_messages().empty());
}

TEST_F(JsonReaderTest, IsOkIsFalseForSchemaNonCompilantJson) {
  rapidjson::Document config;
  config.Parse(kInvalidSchema.data(), kInvalidSchema.size());
  JsonReader reader(std::move(config), GetSchema());

  ASSERT_FALSE(reader.Validate());
  ASSERT_FALSE(reader.IsOk());
  ASSERT_FALSE(reader.error_messages().empty());
}

}  // namespace
}  // namespace broker_service
