// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>

#include <cobalt-client/cpp/in-memory-logger.h>
#include <cobalt-client/cpp/types-internal.h>
#include <zxtest/zxtest.h>

namespace cobalt_client {
namespace {
using MetricInfo = InMemoryLogger::MetricInfo;

constexpr std::string_view kComponent = "SomeComponent";
constexpr uint32_t kMetricId = 1;
constexpr std::array<uint32_t, MetricInfo::kMaxEventCodes> kEventCodes = {0, 1, 2, 3, 4};

MetricInfo MakeMetricInfo(uint32_t metric_id, std::string_view component,
                          const std::array<uint32_t, MetricInfo::kMaxEventCodes>& event_codes) {
  MetricInfo info = {};
  info.metric_id = metric_id;
  info.component = component;
  info.event_codes = event_codes;
  return info;
}

TEST(MetricInfoTest, EqualOperatorIdentity) {
  MetricInfo info = MakeMetricInfo(kMetricId, kComponent, kEventCodes);

  ASSERT_TRUE(info == info);
  ASSERT_FALSE(info != info);
}

TEST(MetricInfoTest, EqualOperatorSameValue) {
  MetricInfo info_1 = MakeMetricInfo(kMetricId, kComponent, kEventCodes);
  MetricInfo info_2 = MakeMetricInfo(kMetricId, kComponent, kEventCodes);

  ASSERT_TRUE(info_1 == info_2);
  ASSERT_FALSE(info_1 != info_2);
}

TEST(MetricInfoTest, EqualOperatorForDifferentValues) {
  MetricInfo info_1 = MakeMetricInfo(kMetricId, kComponent, kEventCodes);
  MetricInfo info_2 = MakeMetricInfo(kMetricId + 1, kComponent, kEventCodes);

  ASSERT_FALSE(info_1 == info_2);
  ASSERT_TRUE(info_1 != info_2);
}

TEST(MetricInfoTest, LessThanIsFalseForEqualInfos) {
  MetricInfo::LessThan less_than;
  MetricInfo info = MakeMetricInfo(kMetricId, kComponent, kEventCodes);

  ASSERT_FALSE(less_than(info, info));
}

TEST(MetricInfoTest, LessThanIsLexicographicWithMetricId) {
  MetricInfo::LessThan less_than;
  MetricInfo info_1 = MakeMetricInfo(kMetricId, kComponent, kEventCodes);
  MetricInfo info_2 = MakeMetricInfo(kMetricId + 1, kComponent, kEventCodes);

  ASSERT_TRUE(less_than(info_1, info_2));
  ASSERT_FALSE(less_than(info_2, info_1));
}

TEST(MetricInfoTest, LessThanIsLexicographicWithEventCodes) {
  MetricInfo::LessThan less_than;
  MetricInfo info_1 = MakeMetricInfo(kMetricId, kComponent, {0, 1, 2, 3, 4});
  MetricInfo info_2 = MakeMetricInfo(kMetricId, kComponent, {0, 1, 2, 3, 5});

  ASSERT_TRUE(less_than(info_1, info_2));
  ASSERT_FALSE(less_than(info_2, info_1));
}

TEST(MetricInfoTest, LessThanIsLexicographicWithComponents) {
  MetricInfo::LessThan less_than;
  MetricInfo info_1 = MakeMetricInfo(kMetricId, "aaa", kEventCodes);
  MetricInfo info_2 = MakeMetricInfo(kMetricId, "aab", kEventCodes);

  ASSERT_TRUE(less_than(info_1, info_2));
  ASSERT_FALSE(less_than(info_2, info_1));
}

}  // namespace
}  // namespace cobalt_client
