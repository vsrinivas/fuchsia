// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cobalt-client/cpp/types-internal.h>

#include <lib/sync/completion.h>
#include <lib/zx/time.h>
#include <threads.h>

#include <cobalt-client/cpp/metric-options.h>
#include <fbl/string.h>
#include <unittest/unittest.h>

namespace cobalt_client {
namespace internal {
namespace {

// Name of component used for options.
constexpr char kComponent[] = "SomeRandomComponent";

constexpr uint32_t kMetricId = 1;

constexpr uint32_t kEventCode = 2;

MetricOptions MakeMetricOptions() {
  MetricOptions options;
  options.component = kComponent;
  options.event_codes = {kEventCode, kEventCode, kEventCode, kEventCode, kEventCode};
  options.metric_id = kMetricId;
  return options;
}

bool TestFromMetricOptions() {
  BEGIN_TEST;
  MetricOptions options = MakeMetricOptions();
  options.SetMode(MetricOptions::Mode::kEager);
  MetricInfo info = MetricInfo::From(options);
  ASSERT_STR_EQ(options.component.c_str(), info.component.c_str());
  ASSERT_TRUE(options.event_codes == info.event_codes);
  END_TEST;
}

bool TestFromMetricOptionsNoComponent() {
  BEGIN_TEST;
  MetricOptions options = MakeMetricOptions();
  options.SetMode(MetricOptions::Mode::kEager);
  options.component.clear();
  MetricInfo info = MetricInfo::From(options);
  ASSERT_TRUE(info.component.empty());
  END_TEST;
}

BEGIN_TEST_CASE(MetricInfo)
RUN_TEST(TestFromMetricOptions)
RUN_TEST(TestFromMetricOptionsNoComponent)
END_TEST_CASE(MetricInfo)

}  // namespace
}  // namespace internal
}  // namespace cobalt_client
