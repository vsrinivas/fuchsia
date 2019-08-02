// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/metric_broker/config/cobalt/metric_config.h"

#include <cstdint>
#include <iterator>
#include <optional>

#include "garnet/bin/metric_broker/config/cobalt/event_codes.h"
#include "garnet/bin/metric_broker/config/cobalt/types.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace broker_service::cobalt {
namespace {

constexpr EventCodes MakeCodes() {
  cobalt::EventCodes codes;
  codes.codes[0] = 0;
  codes.codes[1] = std::nullopt;
  codes.codes[2] = 2;
  codes.codes[3] = 3;
  codes.codes[4] = std::nullopt;
  return codes;
}

constexpr uint64_t kMetricId = 1234;
constexpr SupportedType kMetricType = SupportedType::kHistogram;
constexpr std::string_view kMetricPath = "/some/path";

TEST(MetricConfigTest, InitializationIsOk) {
  MetricConfig config(kMetricId, kMetricType);

  EXPECT_EQ(kMetricId, config.metric_id());
  EXPECT_EQ(kMetricType, config.type());
  EXPECT_EQ(config.begin(), config.end());
  EXPECT_TRUE(config.IsEmpty());
}

TEST(MetricConfigTest, InsertOrUpdateAddsNewMapping) {
  MetricConfig config(kMetricId, kMetricType);
  EventCodes codes = MakeCodes();

  ASSERT_EQ(std::nullopt, config.GetEventCodes(kMetricPath));

  config.InsertOrUpdate(kMetricPath, codes);

  auto event_code = config.GetEventCodes(kMetricPath);
  ASSERT_TRUE(event_code.has_value());
  EXPECT_EQ(codes.codes, event_code.value().codes);
  EXPECT_EQ(1, std::distance(config.begin(), config.end()));
  EXPECT_FALSE(config.IsEmpty());
}

TEST(MetricConfigTest, GetEventCodesFromUnmappedPathIsNullOpt) {
  MetricConfig config(kMetricId, kMetricType);

  ASSERT_EQ(std::nullopt, config.GetEventCodes(kMetricPath));
  ASSERT_EQ(std::nullopt, config.GetEventCodes("some/path/somewhere"));
}

TEST(MetricConfigTest, ClearResetsMappings) {
  MetricConfig config(kMetricId, kMetricType);
  EventCodes codes = MakeCodes();

  ASSERT_TRUE(config.IsEmpty());

  config.InsertOrUpdate(kMetricPath, codes);

  auto event_code = config.GetEventCodes(kMetricPath);
  ASSERT_TRUE(event_code.has_value());

  config.Clear();
  event_code = config.GetEventCodes(kMetricPath);
  EXPECT_EQ(kMetricId, config.metric_id());
  EXPECT_EQ(kMetricType, config.type());
  EXPECT_FALSE(event_code.has_value());
  EXPECT_TRUE(config.IsEmpty());
  EXPECT_EQ(0, std::distance(config.begin(), config.end()));
}

}  // namespace
}  // namespace broker_service::cobalt
