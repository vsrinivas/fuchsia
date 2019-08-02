// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/metric_broker/config/cobalt/project_config.h"

#include <cstdint>
#include <optional>
#include <string_view>

#include "garnet/bin/metric_broker/config/cobalt/event_codes.h"
#include "garnet/bin/metric_broker/config/cobalt/metric_config.h"
#include "garnet/bin/metric_broker/config/cobalt/types.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace broker_service::cobalt {
namespace {

constexpr std::string_view kProjectName = "MyProject";
constexpr uint64_t kUpdateIntervalSec = 1;

constexpr uint64_t kMetricId = 1;
constexpr SupportedType kMetricType = SupportedType::kHistogram;

TEST(ProjectConfigTest, InitializeIsOk) {
  ProjectConfig config(kProjectName, kUpdateIntervalSec);

  EXPECT_EQ(kProjectName, config.project_name());
  EXPECT_EQ(kUpdateIntervalSec, config.update_interval_sec());
  EXPECT_EQ(config.begin(), config.end());
}

TEST(ProjectConfigTest, FindOrCreateMetricAddsNewMetricConfig) {
  ProjectConfig config(kProjectName, kUpdateIntervalSec);

  ASSERT_EQ(config.begin(), config.end());

  auto metric_config = config.FindOrCreate(kMetricId, kMetricType);
  ASSERT_TRUE(metric_config.has_value());

  EXPECT_EQ(1, std::distance(config.begin(), config.end()));
  EXPECT_EQ(metric_config.value(), &(*config.begin()));
  EXPECT_EQ(kMetricId, metric_config.value()->metric_id());
  EXPECT_EQ(kMetricType, metric_config.value()->type());
  EXPECT_FALSE(config.IsEmpty());
}

TEST(ProjectConfigTest, FindOrCreateMetricReturnsExistingMetricConfig) {
  ProjectConfig config(kProjectName, kUpdateIntervalSec);

  ASSERT_TRUE(config.IsEmpty());
  ASSERT_EQ(config.begin(), config.end());

  auto metric_config_existing = config.FindOrCreate(kMetricId, kMetricType);
  ASSERT_TRUE(metric_config_existing.has_value());
  auto metric_config_new = config.FindOrCreate(kMetricId, kMetricType);
  ASSERT_EQ(metric_config_existing.value(), metric_config_new.value());
  ASSERT_FALSE(config.IsEmpty());
  EXPECT_EQ(1, std::distance(config.begin(), config.end()));
}

TEST(ProjectConfigTest, FindOrCreateMetricReturnsNullOptOnTypeMismatch) {
  ProjectConfig config(kProjectName, kUpdateIntervalSec);

  ASSERT_TRUE(config.IsEmpty());
  ASSERT_EQ(config.begin(), config.end());
  auto metric_config_existing = config.FindOrCreate(kMetricId, SupportedType::kCounter);
  ASSERT_TRUE(metric_config_existing.has_value());
  auto metric_config_new = config.FindOrCreate(kMetricId, SupportedType::kHistogram);

  EXPECT_FALSE(metric_config_new.has_value());
  EXPECT_EQ(1, std::distance(config.begin(), config.end()));
}

TEST(ProjectConfigTest, FindReturnsNullOptOnUnregisteredMetricConfig) {
  ProjectConfig config(kProjectName, kUpdateIntervalSec);
  ASSERT_TRUE(config.IsEmpty());
  ASSERT_EQ(config.begin(), config.end());

  auto metric_config = config.Find(kMetricId);
  ASSERT_FALSE(metric_config.has_value());
  ASSERT_TRUE(config.IsEmpty());
}

TEST(ProjectConfigTest, FindReturnsRegisteredMetricConfig) {
  ProjectConfig config(kProjectName, kUpdateIntervalSec);

  ASSERT_EQ(config.begin(), config.end());

  auto metric_config_existing = config.FindOrCreate(kMetricId, SupportedType::kCounter);
  auto metric_config_existing_1 = config.FindOrCreate(kMetricId + 1, SupportedType::kCounter);
  auto metric_config_existing_2 = config.FindOrCreate(kMetricId + 2, SupportedType::kCounter);

  ASSERT_TRUE(metric_config_existing.has_value());
  ASSERT_TRUE(metric_config_existing_1.has_value());
  ASSERT_TRUE(metric_config_existing_2.has_value());
  ASSERT_EQ(3, std::distance(config.begin(), config.end()));
  ASSERT_FALSE(config.IsEmpty());

  auto metric_config_new = config.Find(kMetricId);
  auto metric_config_new_1 = config.Find(kMetricId + 1);
  auto metric_config_new_2 = config.Find(kMetricId + 2);

  EXPECT_EQ(metric_config_existing.value(), metric_config_new.value());
  EXPECT_EQ(metric_config_existing_1.value(), metric_config_new_1.value());
  EXPECT_EQ(metric_config_existing_2.value(), metric_config_new_2.value());
  EXPECT_EQ(3, std::distance(config.begin(), config.end()));
}

TEST(ProjectConfigTest, ClearRemovesRegisteredMetricConfigs) {
  ProjectConfig config(kProjectName, kUpdateIntervalSec);

  ASSERT_TRUE(config.IsEmpty());
  ASSERT_EQ(config.begin(), config.end());

  ASSERT_TRUE(config.FindOrCreate(kMetricId, SupportedType::kCounter).has_value());
  ASSERT_TRUE(config.FindOrCreate(kMetricId + 1, SupportedType::kCounter).has_value());
  ASSERT_TRUE(config.FindOrCreate(kMetricId + 2, SupportedType::kCounter).has_value());
  ASSERT_FALSE(config.IsEmpty());

  config.Clear();

  EXPECT_EQ(kProjectName, config.project_name());
  EXPECT_EQ(kUpdateIntervalSec, config.update_interval_sec());
  EXPECT_FALSE(config.Find(kMetricId).has_value());
  EXPECT_FALSE(config.Find(kMetricId + 1).has_value());
  EXPECT_FALSE(config.Find(kMetricId + 2).has_value());
  EXPECT_EQ(0, std::distance(config.begin(), config.end()));
}

}  // namespace
}  // namespace broker_service::cobalt
