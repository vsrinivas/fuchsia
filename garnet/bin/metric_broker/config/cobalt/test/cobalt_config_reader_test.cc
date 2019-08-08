// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/metric_broker/config/cobalt/cobalt_config_reader.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "garnet/bin/metric_broker/config/cobalt/event_codes.h"
#include "garnet/bin/metric_broker/config/cobalt/metric_config.h"
#include "garnet/bin/metric_broker/config/cobalt/project_config.h"
#include "garnet/bin/metric_broker/config/cobalt/types.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/schema.h"
#include "rapidjson/stringbuffer.h"

namespace broker_service::cobalt {
namespace {

// This project contains 2 metrics and three mappings.
constexpr std::string_view kMinimalProjectConfig = R"(
    {
        "project": {
            "name": "my_project",
            "update_interval_seconds": 20
        },
        "metrics": [
            {
                "id": 1,
                "metric_type": "HISTOGRAM"
            },
            {
                "id": 2,
                "metric_type": "COUNTER"
            }
        ],
        "mappings": [
           {
               "metric_id": 1,
               "path":"my/path/1",
               "event_codes": [
                   null,
                   null,
                   {"value": 1},
                   null,
                   {"value": 2}
               ]
           },
           {
               "metric_id": 1,
               "path":"my/path/3",
               "event_codes": [
                   null,
                   null,
                   null,
                   null,
                   {"value": 3}
               ]
           },
           {
               "metric_id": 2,
               "path":"my/path/4",
               "event_codes": [
                   null,
                   {"value": 3},
                   null,
                   null,
                   null
               ]
           }
        ]
    }
)";

constexpr std::string_view kSchemaPath = "pkg/data/testdata/cobalt/config.schema.json";

constexpr std::string_view kProjectName = "my_project";
constexpr uint64_t kUpdateIntervalMs = 20;

constexpr uint64_t kFirstMetricId = 1;
constexpr SupportedType kFirstMetricType = SupportedType::kHistogram;

constexpr uint64_t kSecondMetricId = 2;
constexpr SupportedType kSecondMetricType = SupportedType::kCounter;

constexpr std::string_view kFirstMappingPath = "my/path/1";
constexpr uint64_t kFirstMappingMetricId = 1;
constexpr std::array<EventCodes::CodeType, kMaxDimensionsPerEvent> kFirstMappingEvents = {
    std::nullopt, std::nullopt, 1, std::nullopt, 2};

constexpr std::string_view kSecondMappingPath = "my/path/3";
constexpr uint64_t kSecondMappingMetricId = 1;
constexpr std::array<EventCodes::CodeType, kMaxDimensionsPerEvent> kSecondMappingEvents = {
    std::nullopt, std::nullopt, std::nullopt, std::nullopt, 3};

constexpr std::string_view kThirdMappingPath = "my/path/4";
constexpr uint64_t kThirdMappingMetricId = 2;
constexpr std::array<EventCodes::CodeType, kMaxDimensionsPerEvent> kThirdMappingEvents = {
    std::nullopt, 3, std::nullopt, std::nullopt, std::nullopt};

class CobaltConfigReaderTest : public ::testing::Test {
 public:
  static void SetUpTestSuite() {
    std::ifstream config_stream(kSchemaPath.data());
    ASSERT_TRUE(config_stream.good()) << strerror(errno);
    rapidjson::IStreamWrapper config_wrapper(config_stream);
    rapidjson::Document schema_doc;
    ASSERT_FALSE(schema_doc.ParseStream(config_wrapper).HasParseError());
    schema_ = std::make_unique<rapidjson::SchemaDocument>(schema_doc);

    rapidjson::Document config = GetProjectConfig();
    ASSERT_FALSE(config.HasParseError());
    rapidjson::SchemaValidator validator(*schema_);
    ASSERT_TRUE(config.Accept(validator)) << GetError(validator);
  }

  static void TearDownTestSuite() { schema_.reset(); }

  [[nodiscard]] static rapidjson::Document GetProjectConfig() {
    rapidjson::Document config;
    config.Parse(kMinimalProjectConfig.data(), kMinimalProjectConfig.size());
    return config;
  }

  [[nodiscard]] static rapidjson::Document GetBadProjectConfig() {
    rapidjson::Document config;
    config.Parse("{}");
    return config;
  }

 protected:
  static std::unique_ptr<rapidjson::SchemaDocument> schema_;

  static std::string GetError(const rapidjson::SchemaValidator& validator) {
    rapidjson::StringBuffer sb, sb2;
    validator.GetInvalidSchemaPointer().StringifyUriFragment(sb);
    validator.GetInvalidSchemaPointer().StringifyUriFragment(sb2);
    return "Invalid schema: " + std::string(sb.GetString()) +
           "\nInvalid Keyword: " + std::string(validator.GetInvalidSchemaKeyword()) +
           "\nInvalid Document: " + std::string(sb2.GetString()) + "\n";
  }
};

std::unique_ptr<rapidjson::SchemaDocument> CobaltConfigReaderTest::schema_ = nullptr;

TEST_F(CobaltConfigReaderTest, ReadProjectIsOk) {
  CobaltConfigReader reader(GetProjectConfig(), schema_.get());
  ASSERT_TRUE(reader.IsOk());

  auto project_config_opt = reader.ReadProject();
  ASSERT_TRUE(project_config_opt.has_value());
  auto* project_config = project_config_opt.value();

  ASSERT_NE(nullptr, project_config);

  EXPECT_EQ(kProjectName, project_config->project_name());
  EXPECT_EQ(kUpdateIntervalMs, project_config->update_interval_sec());
  // No call to Read Metrics yet.
  EXPECT_EQ(project_config->begin(), project_config->end());
}

TEST_F(CobaltConfigReaderTest, ReadMetricsIsOk) {
  CobaltConfigReader reader(GetProjectConfig(), schema_.get());
  ASSERT_TRUE(reader.IsOk());

  // Initialize the project.
  [[maybe_unused]] auto project_config_opt = reader.ReadProject();

  // Read First metric
  {
    auto metric_config_opt = reader.ReadNextMetric();
    ASSERT_TRUE(metric_config_opt.has_value());
    auto* metric_config = metric_config_opt.value();

    ASSERT_NE(nullptr, metric_config);

    EXPECT_EQ(kFirstMetricId, metric_config->metric_id());
    EXPECT_EQ(kFirstMetricType, metric_config->type());

    // No call to ReadNextMapping yet.
    EXPECT_EQ(metric_config->begin(), metric_config->end());
  }
  // Both metrics are registered after reading.
  ASSERT_EQ(1,
            std::distance(project_config_opt.value()->begin(), project_config_opt.value()->end()));

  // Read Second metric
  {
    auto metric_config_opt = reader.ReadNextMetric();
    ASSERT_TRUE(metric_config_opt.has_value()) << reader.error_messages()[0];
    auto* metric_config = metric_config_opt.value();

    ASSERT_NE(nullptr, metric_config);

    EXPECT_EQ(kSecondMetricId, metric_config->metric_id());
    EXPECT_EQ(kSecondMetricType, metric_config->type());

    // No call to ReadNextMapping yet.
    EXPECT_EQ(metric_config->begin(), metric_config->end());
  }
  // Both metrics are registered after reading.
  ASSERT_EQ(2,
            std::distance(project_config_opt.value()->begin(), project_config_opt.value()->end()));

  // Next call returns std::nullopt since there are no more metrics.
  {
    auto metric_config_opt = reader.ReadNextMetric();
    ASSERT_FALSE(metric_config_opt.has_value());
  }

  // Both metrics are registered after reading.
  ASSERT_EQ(2,
            std::distance(project_config_opt.value()->begin(), project_config_opt.value()->end()));
}

TEST_F(CobaltConfigReaderTest, ReadMetricMappingIsOk) {
  CobaltConfigReader reader(GetProjectConfig(), schema_.get());
  ASSERT_TRUE(reader.IsOk());
  auto project_config = reader.ReadProject();
  ASSERT_TRUE(project_config.has_value());

  auto metric_1 = reader.ReadNextMetric();
  ASSERT_TRUE(metric_1.has_value());
  auto metric_2 = reader.ReadNextMetric();
  ASSERT_TRUE(metric_2.has_value());
  ASSERT_FALSE(reader.ReadNextMetric().has_value());

  // Read first mapping.
  {
    auto mapping = reader.ReadNextMapping();
    EXPECT_TRUE(mapping.has_value());
    EXPECT_EQ(kFirstMappingMetricId, mapping.value().metric_id);
    EXPECT_EQ(kFirstMappingPath, mapping.value().path);
    EXPECT_THAT(mapping.value().codes.codes, kFirstMappingEvents);
    EXPECT_EQ(1, std::distance(metric_1.value()->begin(), metric_1.value()->end()));
  }

  // Read second mapping.
  {
    auto mapping = reader.ReadNextMapping();
    EXPECT_TRUE(mapping.has_value());
    EXPECT_EQ(kSecondMappingMetricId, mapping.value().metric_id);
    EXPECT_EQ(kSecondMappingPath, mapping.value().path);
    EXPECT_THAT(mapping.value().codes.codes, kSecondMappingEvents);
    EXPECT_EQ(2, std::distance(metric_1.value()->begin(), metric_1.value()->end()));
  }

  // Read third mapping.
  {
    auto mapping = reader.ReadNextMapping();
    EXPECT_TRUE(mapping.has_value());
    EXPECT_EQ(kThirdMappingMetricId, mapping.value().metric_id);
    EXPECT_EQ(kThirdMappingPath, mapping.value().path);
    EXPECT_THAT(mapping.value().codes.codes, kThirdMappingEvents);
    EXPECT_EQ(1, std::distance(metric_2.value()->begin(), metric_2.value()->end()));
  }

  // Last mapping (End())
  {
    auto mapping = reader.ReadNextMapping();
    EXPECT_FALSE(mapping.has_value());
  }
}

TEST_F(CobaltConfigReaderTest, ReadProjectReturnsNullOptWhenNotOk) {
  CobaltConfigReader reader(GetBadProjectConfig(), schema_.get());
  ASSERT_FALSE(reader.Validate());
  ASSERT_FALSE(reader.IsOk());

  ASSERT_EQ(std::nullopt, reader.ReadProject());
}

TEST_F(CobaltConfigReaderTest, ReadMetricReturnsNullOptWhenNotOk) {
  CobaltConfigReader reader(GetBadProjectConfig(), schema_.get());
  ASSERT_FALSE(reader.Validate());
  ASSERT_FALSE(reader.IsOk());

  ASSERT_EQ(std::nullopt, reader.ReadNextMetric());
}

TEST_F(CobaltConfigReaderTest, ReadMetricMappingReturnsNullOptWhenNotOk) {
  CobaltConfigReader reader(GetBadProjectConfig(), schema_.get());
  ASSERT_FALSE(reader.Validate());
  ASSERT_FALSE(reader.IsOk());

  ASSERT_EQ(std::nullopt, reader.ReadNextMapping());
}

TEST_F(CobaltConfigReaderTest, MakeProjectAndTakeIsOk) {
  CobaltConfigReader reader(GetProjectConfig(), schema_.get());
  ASSERT_TRUE(reader.IsOk());

  std::unique_ptr<ProjectConfig> project_config = reader.MakeProjectAndReset().value();

  ASSERT_EQ(kProjectName, project_config->project_name());
  ASSERT_EQ(kUpdateIntervalMs, project_config->update_interval_sec());

  // Check that next call to ReadProject has a different address.
  ASSERT_NE(project_config.get(), reader.ReadProject());

  // Check Metric with Id 1 is correct.
  {
    auto* metric_config = project_config->Find(kFirstMetricId).value_or(nullptr);
    ASSERT_NE(nullptr, metric_config);

    EXPECT_EQ(kFirstMetricId, metric_config->metric_id());
    EXPECT_EQ(kFirstMetricType, metric_config->type());

    EXPECT_EQ(2, std::distance(metric_config->begin(), metric_config->end()));
    // Check first mapping.
    {
      auto event_codes_opt = metric_config->GetEventCodes(kFirstMappingPath);
      ASSERT_TRUE(event_codes_opt.has_value());
      auto event_codes = event_codes_opt.value();
      ASSERT_THAT(event_codes.codes, ::testing::ElementsAreArray(kFirstMappingEvents));
    }

    // Check second mapping.
    {
      auto event_codes_opt = metric_config->GetEventCodes(kSecondMappingPath);
      ASSERT_TRUE(event_codes_opt.has_value());
      auto event_codes = event_codes_opt.value();
      ASSERT_THAT(event_codes.codes, ::testing::ElementsAreArray(kSecondMappingEvents));
    }
  }

  // Check Metric with Id 2 is correct.
  {
    auto* metric_config = project_config->Find(kSecondMetricId).value_or(nullptr);
    ASSERT_NE(nullptr, metric_config);

    EXPECT_EQ(kSecondMetricId, metric_config->metric_id());
    EXPECT_EQ(kSecondMetricType, metric_config->type());

    EXPECT_EQ(1, std::distance(metric_config->begin(), metric_config->end()));
    // Check first mapping.
    {
      auto event_codes_opt = metric_config->GetEventCodes(kThirdMappingPath);
      ASSERT_TRUE(event_codes_opt.has_value());
      auto event_codes = event_codes_opt.value();
      ASSERT_THAT(event_codes.codes, ::testing::ElementsAreArray(kThirdMappingEvents));
    }
  }
}

TEST_F(CobaltConfigReaderTest, ResetClearsAllState) {
  CobaltConfigReader reader(GetProjectConfig(), schema_.get());
  ASSERT_TRUE(reader.IsOk());

  // Just all metrics and mappings.
  reader.ReadProject();
  reader.ReadNextMetric();
  reader.ReadNextMetric();
  reader.ReadNextMapping();
  reader.ReadNextMapping();
  reader.ReadNextMapping();

  ASSERT_FALSE(reader.ReadNextMetric().has_value());
  ASSERT_FALSE(reader.ReadNextMapping().has_value());

  reader.Reset();
  // This now calls to read metrics and mappings should not return std::nullopt.
  reader.ReadProject();
  ASSERT_TRUE(reader.ReadNextMetric().has_value());
  ASSERT_TRUE(reader.ReadNextMapping().has_value());
}

}  // namespace
}  // namespace broker_service::cobalt
