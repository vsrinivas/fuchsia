// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <threads.h>

#include <cobalt-client/cpp/metric-options.h>
#include <cobalt-client/cpp/types-internal.h>
#include <fbl/string.h>
#include <lib/sync/completion.h>
#include <lib/zx/time.h>
#include <unittest/unittest.h>

namespace cobalt_client {
namespace internal {
namespace {

// Name of component used for options.
constexpr char kComponent[] = "SomeRandomComponent";

constexpr uint32_t kMetricId = 1;

constexpr uint32_t kEventCode = 2;

const char* GetMetricName(uint32_t metric_id) {
    if (metric_id == kMetricId) {
        return "MetricName";
    }
    return "UnknownMetric";
}

const char* GetEventName(uint32_t event_code) {
    if (event_code == kEventCode) {
        return "EventName";
    }
    return "UnknownEvent";
}

MetricOptions MakeMetricOptions() {
    MetricOptions options;
    options.component = kComponent;
    options.event_code = kEventCode;
    options.metric_id = kMetricId;
    options.get_metric_name = GetMetricName;
    options.get_event_name = GetEventName;
    return options;
}

bool TestFromMetricOptions() {
    BEGIN_TEST;
    MetricOptions options = MakeMetricOptions();
    options.SetMode(MetricOptions::Mode::kRemoteAndLocal);
    LocalMetricInfo info = LocalMetricInfo::From(options);
    ASSERT_STR_EQ(info.name.c_str(), "MetricName.SomeRandomComponent.EventName");
    END_TEST;
}

bool TestFromMetricOptionsNoGetMetricName() {
    BEGIN_TEST;
    MetricOptions options = MakeMetricOptions();
    options.SetMode(MetricOptions::Mode::kRemoteAndLocal);
    options.get_metric_name = nullptr;
    LocalMetricInfo info = LocalMetricInfo::From(options);
    ASSERT_STR_EQ(info.name.c_str(), "1.SomeRandomComponent.EventName");
    END_TEST;
}

bool TestFromMetricOptionsNoGetEventName() {
    BEGIN_TEST;
    MetricOptions options = MakeMetricOptions();
    options.SetMode(MetricOptions::Mode::kRemoteAndLocal);
    options.get_event_name = nullptr;
    LocalMetricInfo info = LocalMetricInfo::From(options);
    ASSERT_STR_EQ(info.name.c_str(), "MetricName.SomeRandomComponent.2");
    END_TEST;
}

bool TestFromMetricOptionsNoComponent() {
    BEGIN_TEST;
    MetricOptions options = MakeMetricOptions();
    options.SetMode(MetricOptions::Mode::kRemoteAndLocal);
    options.component.clear();
    LocalMetricInfo info = LocalMetricInfo::From(options);
    ASSERT_STR_EQ(info.name.c_str(), "MetricName.EventName");
    END_TEST;
}

BEGIN_TEST_CASE(LocalMetricInfo)
RUN_TEST(TestFromMetricOptions)
RUN_TEST(TestFromMetricOptionsNoComponent)
RUN_TEST(TestFromMetricOptionsNoGetMetricName)
RUN_TEST(TestFromMetricOptionsNoGetEventName)
END_TEST_CASE(LocalMetricInfo)

} // namespace
} // namespace internal
} // namespace cobalt_client
