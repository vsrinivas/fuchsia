// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/metrics/cobalt_metrics.h"

#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <unistd.h>

#include <utility>
#include <vector>

#include <cobalt-client/cpp/collector.h>
#include <cobalt-client/cpp/counter.h>
#include <cobalt-client/cpp/histogram.h>
#include <cobalt-client/cpp/in_memory_logger.h>
#include <zxtest/zxtest.h>

namespace fs_metrics {
namespace {

// Observed latency.
constexpr uint32_t kLatencyNs = 5000;

std::unique_ptr<cobalt_client::Collector> MakeCollector(
    cobalt_client::InMemoryLogger** logger = nullptr) {
  auto logger_ptr = std::make_unique<cobalt_client::InMemoryLogger>();
  if (logger != nullptr) {
    *logger = logger_ptr.get();
  }
  return std::make_unique<cobalt_client::Collector>(std::move(logger_ptr));
}

TEST(CobaltMetricsTest, LogWhileEnabled) {
  fs_metrics::Metrics metrics(MakeCollector(), Source::kUnknown);
  metrics.EnableMetrics(/*should_collect*/ true);

  fs_metrics::FsCommonMetrics* vnodes = metrics.mutable_fs_common_metrics();
  ASSERT_NOT_NULL(vnodes);
  if (metrics.IsEnabled()) {
    vnodes->vnode.close.Add(kLatencyNs);
  }
  // We should have observed 15 hundred usecs.
  EXPECT_EQ(vnodes->vnode.close.GetCount(kLatencyNs), 1);
}

TEST(CobaltMetricsTest, LogWhileNotEnabled) {
  fs_metrics::Metrics metrics(MakeCollector(), Source::kUnknown);
  metrics.EnableMetrics(/*should_collect*/ false);

  fs_metrics::FsCommonMetrics* vnodes = metrics.mutable_fs_common_metrics();
  ASSERT_NOT_NULL(vnodes);
  if (metrics.IsEnabled()) {
    vnodes->vnode.close.Add(kLatencyNs);
  }
  EXPECT_EQ(vnodes->vnode.close.GetCount(kLatencyNs), 0);
}

TEST(CobaltMetricsTest, EnableMetricsEnabled) {
  fs_metrics::Metrics metrics(MakeCollector(), Source::kUnknown);
  fs_metrics::FsCommonMetrics* vnodes = metrics.mutable_fs_common_metrics();
  ASSERT_NOT_NULL(vnodes);
  ASSERT_EQ(vnodes->metrics_enabled, metrics.IsEnabled());
  metrics.EnableMetrics(/*should_collect*/ true);

  EXPECT_TRUE(metrics.IsEnabled());
  EXPECT_TRUE(vnodes->metrics_enabled);
}

TEST(CobaltMetricsTest, EnableMetricsDisabled) {
  fs_metrics::Metrics metrics(MakeCollector(), Source::kUnknown);
  metrics.EnableMetrics(/*should_collect*/ true);
  fs_metrics::FsCommonMetrics* vnodes = metrics.mutable_fs_common_metrics();

  ASSERT_NOT_NULL(vnodes);
  ASSERT_EQ(vnodes->metrics_enabled, metrics.IsEnabled());
  metrics.EnableMetrics(/*should_collect*/ false);

  EXPECT_FALSE(metrics.IsEnabled());
  EXPECT_FALSE(vnodes->metrics_enabled);
}

TEST(CobaltMetricsTest, EventSourceSetInMetricOptions) {
  constexpr Source source = Source::kBlobfs;
  constexpr uint32_t source_event_code = static_cast<uint32_t>(source);
  fs_metrics::Metrics metrics(MakeCollector(), source);

  const auto& fs_common_metrics = metrics.fs_common_metrics();
  EXPECT_EQ(fs_common_metrics.vnode.close.GetOptions().event_codes[0], source_event_code);
  EXPECT_EQ(fs_common_metrics.journal.write_data.GetOptions().event_codes[0], source_event_code);
}

}  // namespace
}  // namespace fs_metrics
