// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <cobalt-client/cpp/collector.h>
#include <cobalt-client/cpp/counter.h>
#include <cobalt-client/cpp/histogram.h>
#include <fbl/unique_ptr.h>
#include <fs/metrics.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <unittest/unittest.h>

#include <utility>

namespace fs {
namespace {

// Observed latency.
constexpr uint32_t kLatencyNs = 5000;

constexpr uint32_t kBuckets = 20;

cobalt_client::CollectorOptions MakeOptions() {
    cobalt_client::CollectorOptions options = cobalt_client::CollectorOptions::Debug();
    options.load_config = [](zx::vmo*, size_t*) { return false; };
    options.initial_response_deadline = zx::duration(0);
    options.response_deadline = zx::duration(0);
    return options;
}

cobalt_client::HistogramOptions MakeHistogramOptions() {
    cobalt_client::HistogramOptions options =
        cobalt_client::HistogramOptions::CustomizedExponential(10, 2, 1, 0);
    options.SetMode(cobalt_client::MetricOptions::Mode::kRemote);
    options.metric_id = 1;
    options.event_code = 0;
    return options;
}

cobalt_client::MetricOptions MakeCounterOptions() {
    cobalt_client::MetricOptions options;
    options.SetMode(cobalt_client::MetricOptions::Mode::kRemote);
    options.metric_id = 1;
    options.event_code = 0;
    return options;
}

bool TestLogWhileEnabled() {
    BEGIN_TEST;
    fs::Metrics metrics(MakeOptions(), /*local_metrics*/ false, "TestFs");
    metrics.EnableMetrics(/*should_collect*/ true);

    fs::VnodeMetrics* vnodes = metrics.mutable_vnode_metrics();
    ASSERT_NE(vnodes, nullptr);
    if (metrics.IsEnabled()) {
        vnodes->close.Add(kLatencyNs);
    }
    // We should have observed 15 hundred usecs.
    ASSERT_EQ(vnodes->close.GetRemoteCount(kLatencyNs), 1);
    END_TEST;
}

bool TestLogWhileNotEnabled() {
    BEGIN_TEST;
    fs::Metrics metrics(MakeOptions(), /*local_metrics*/ false, "TestFs");
    metrics.EnableMetrics(/*should_collect*/ false);

    fs::VnodeMetrics* vnodes = metrics.mutable_vnode_metrics();
    ASSERT_NE(vnodes, nullptr);
    if (metrics.IsEnabled()) {
        vnodes->close.Add(kLatencyNs);
    }
    ASSERT_EQ(vnodes->close.GetRemoteCount(kLatencyNs), 0);
    END_TEST;
}

bool TestEnableMetricsEnabled() {
    BEGIN_TEST;
    fs::Metrics metrics(MakeOptions(), /*local_metrics*/ false, "TestFs");
    fs::VnodeMetrics* vnodes = metrics.mutable_vnode_metrics();
    ASSERT_NE(vnodes, nullptr);
    ASSERT_EQ(vnodes->metrics_enabled, metrics.IsEnabled());
    metrics.EnableMetrics(/*should_collect*/ true);
    ASSERT_TRUE(metrics.IsEnabled());
    ASSERT_TRUE(vnodes->metrics_enabled);
    END_TEST;
}

bool TestEnableMetricsDisabled() {
    BEGIN_TEST;
    fs::Metrics metrics(MakeOptions(), /*local_metrics*/ false, "TestFs");
    metrics.EnableMetrics(/*should_collect*/ true);
    fs::VnodeMetrics* vnodes = metrics.mutable_vnode_metrics();
    ASSERT_NE(vnodes, nullptr);
    ASSERT_EQ(vnodes->metrics_enabled, metrics.IsEnabled());
    metrics.EnableMetrics(/*should_collect*/ false);
    ASSERT_FALSE(metrics.IsEnabled());
    ASSERT_FALSE(vnodes->metrics_enabled);
    END_TEST;
}

bool TestAddCustomMetric() {
    BEGIN_TEST;
    fs::Metrics metrics(MakeOptions(), /*local_metrics*/ false, "TestFs");
    metrics.EnableMetrics(/*should_collect*/ false);

    cobalt_client::Histogram<kBuckets> hist =
        cobalt_client::Histogram<kBuckets>(MakeHistogramOptions(), metrics.mutable_collector());
    cobalt_client::Counter counter =
        cobalt_client::Counter(MakeCounterOptions(), metrics.mutable_collector());

    hist.Add(25);
    counter.Increment(20);

    ASSERT_EQ(hist.GetRemoteCount(25), 1);
    ASSERT_EQ(counter.GetRemoteCount(), 20);

    // Sanity check.
    metrics.mutable_collector()->Flush();
    END_TEST;
}

BEGIN_TEST_CASE(MetricsTest)
RUN_TEST(TestLogWhileEnabled)
RUN_TEST(TestLogWhileNotEnabled)
RUN_TEST(TestEnableMetricsEnabled)
RUN_TEST(TestEnableMetricsDisabled)
RUN_TEST(TestAddCustomMetric)
END_TEST_CASE(MetricsTest)

} // namespace
} // namespace fs
