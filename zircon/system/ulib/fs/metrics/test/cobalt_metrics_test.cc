// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <unistd.h>

#include <utility>

#include <cobalt-client/cpp/collector.h>
#include <cobalt-client/cpp/counter.h>
#include <cobalt-client/cpp/histogram.h>
#include <cobalt-client/cpp/in_memory_logger.h>
#include <fs/metrics/cobalt_metrics.h>
#include <zxtest/zxtest.h>

namespace fs_metrics {
namespace {

// Observed latency.
constexpr uint32_t kLatencyNs = 5000;

constexpr uint32_t kBuckets = 20;

std::unique_ptr<cobalt_client::Collector> MakeCollector(
    cobalt_client::InMemoryLogger** logger = nullptr) {
  auto logger_ptr = std::make_unique<cobalt_client::InMemoryLogger>();
  if (logger != nullptr) {
    *logger = logger_ptr.get();
  }
  return std::make_unique<cobalt_client::Collector>(std::move(logger_ptr));
}

cobalt_client::HistogramOptions MakeHistogramOptions() {
  cobalt_client::HistogramOptions options =
      cobalt_client::HistogramOptions::CustomizedExponential(10, 2, 1, 0);
  options.metric_id = 1;
  options.event_codes = {0, 0, 0, 0, 0};
  return options;
}

cobalt_client::MetricOptions MakeCounterOptions() {
  cobalt_client::MetricOptions options;
  options.metric_id = 1;
  options.event_codes = {0, 0, 0, 0, 0};
  return options;
}

TEST(CobaltMetricsTest, LogWhileEnabled) {
  fs_metrics::Metrics metrics(MakeCollector(), "TestFs");
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
  fs_metrics::Metrics metrics(MakeCollector(), "TestFs");
  metrics.EnableMetrics(/*should_collect*/ false);

  fs_metrics::FsCommonMetrics* vnodes = metrics.mutable_fs_common_metrics();
  ASSERT_NOT_NULL(vnodes);
  if (metrics.IsEnabled()) {
    vnodes->vnode.close.Add(kLatencyNs);
  }
  EXPECT_EQ(vnodes->vnode.close.GetCount(kLatencyNs), 0);
}

TEST(CobaltMetricsTest, EnableMetricsEnabled) {
  fs_metrics::Metrics metrics(MakeCollector(), "TestFs");
  fs_metrics::FsCommonMetrics* vnodes = metrics.mutable_fs_common_metrics();
  ASSERT_NOT_NULL(vnodes);
  ASSERT_EQ(vnodes->metrics_enabled, metrics.IsEnabled());
  metrics.EnableMetrics(/*should_collect*/ true);

  EXPECT_TRUE(metrics.IsEnabled());
  EXPECT_TRUE(vnodes->metrics_enabled);
}

TEST(CobaltMetricsTest, EnableMetricsDisabled) {
  fs_metrics::Metrics metrics(MakeCollector(), "TestFs");
  metrics.EnableMetrics(/*should_collect*/ true);
  fs_metrics::FsCommonMetrics* vnodes = metrics.mutable_fs_common_metrics();

  ASSERT_NOT_NULL(vnodes);
  ASSERT_EQ(vnodes->metrics_enabled, metrics.IsEnabled());
  metrics.EnableMetrics(/*should_collect*/ false);

  EXPECT_FALSE(metrics.IsEnabled());
  EXPECT_FALSE(vnodes->metrics_enabled);
}

TEST(CobaltMetricsTest, AddCustomMetric) {
  fs_metrics::Metrics metrics(MakeCollector(), "TestFs");
  metrics.EnableMetrics(/*should_collect*/ false);

  cobalt_client::Histogram<kBuckets> hist =
      cobalt_client::Histogram<kBuckets>(MakeHistogramOptions(), metrics.mutable_collector());
  cobalt_client::Counter counter =
      cobalt_client::Counter(MakeCounterOptions(), metrics.mutable_collector());

  hist.Add(25);
  counter.Increment(20);

  ASSERT_EQ(hist.GetCount(25), 1);
  ASSERT_EQ(counter.GetCount(), 20);

  // Sanity check.
  metrics.mutable_collector()->Flush();
}

TEST(CobaltMetricsTest, CreateCompressionFormatMetrics) {
  cobalt_client::InMemoryLogger* logger;

  fs_metrics::Metrics metrics_unknownfs(MakeCollector(&logger), "TestFs");
  ASSERT_EQ(metrics_unknownfs.compression_format_metrics().source,
            fs_metrics::CompressionSource::kUnknown);
  // No compression format counters for an unknown fs.
  ASSERT_TRUE(metrics_unknownfs.compression_format_metrics().counters.empty());

  fs_metrics::Metrics metrics(MakeCollector(&logger), "blobfs", CompressionSource::kBlobfs);
  metrics.EnableMetrics(/*should_collect*/ true);
  // Compression format counters created for blobfs.
  ASSERT_EQ(metrics.compression_format_metrics().counters.size(),
            static_cast<size_t>(fs_metrics::CompressionFormat::kNumFormats));

  auto source = fs_metrics::CompressionSource::kBlobfs;
  ASSERT_EQ(metrics.compression_format_metrics().source, source);

  std::vector<fs_metrics::CompressionFormat> formats = {
      fs_metrics::CompressionFormat::kUnknown,
      fs_metrics::CompressionFormat::kUncompressed,
      fs_metrics::CompressionFormat::kCompressedLZ4,
      fs_metrics::CompressionFormat::kCompressedZSTD,
      fs_metrics::CompressionFormat::kCompressedZSTDSeekable,
      fs_metrics::CompressionFormat::kCompressedZSTDChunked,
  };

  for (const auto fmt : formats) {
    // Counters don't make it to the logger before the collector is Flush()'d.
    ASSERT_EQ(logger->counters().find(
                  fs_metrics::CompressionFormatMetrics::MakeCompressionMetricOptions(source, fmt)),
              logger->counters().end());
  }

  ASSERT_TRUE(metrics.mutable_collector()->Flush());
  for (const auto fmt : formats) {
    // Counters exist after Flush().
    ASSERT_NE(logger->counters().find(
                  fs_metrics::CompressionFormatMetrics::MakeCompressionMetricOptions(source, fmt)),
              logger->counters().end());
    ASSERT_EQ(logger->counters().at(
                  fs_metrics::CompressionFormatMetrics::MakeCompressionMetricOptions(source, fmt)),
              0);
  }
}

TEST(CobaltMetricsTest, IncrementCompressionFormatMetrics) {
  cobalt_client::InMemoryLogger* logger;
  fs_metrics::Metrics metrics(MakeCollector(&logger), "blobfs", CompressionSource::kBlobfs);
  metrics.EnableMetrics(/*should_collect*/ true);

  auto source = fs_metrics::CompressionSource::kBlobfs;
  std::vector<fs_metrics::CompressionFormat> formats = {
      fs_metrics::CompressionFormat::kUnknown,
      fs_metrics::CompressionFormat::kUncompressed,
      fs_metrics::CompressionFormat::kCompressedLZ4,
      fs_metrics::CompressionFormat::kCompressedZSTD,
      fs_metrics::CompressionFormat::kCompressedZSTDSeekable,
      fs_metrics::CompressionFormat::kCompressedZSTDChunked,
  };

  // No counters incremented yet.
  for (const auto fmt : formats) {
    ASSERT_EQ(metrics.compression_format_metrics().counters.at(fmt)->GetCount(), 0);
  }

  ASSERT_TRUE(metrics.mutable_collector()->Flush());
  for (const auto fmt : formats) {
    // Counters exist after Flush().
    ASSERT_NE(logger->counters().find(
                  fs_metrics::CompressionFormatMetrics::MakeCompressionMetricOptions(source, fmt)),
              logger->counters().end());
    ASSERT_EQ(logger->counters().at(
                  fs_metrics::CompressionFormatMetrics::MakeCompressionMetricOptions(source, fmt)),
              0);
  }

  // Increment counters for a couple of formats.
  auto fmt1 = fs_metrics::CompressionFormat::kUncompressed;
  auto fmt2 = fs_metrics::CompressionFormat::kCompressedLZ4;

  metrics.mutable_compression_format_metrics()->IncrementCounter(fmt1, 10);
  ASSERT_EQ(metrics.compression_format_metrics().counters.at(fmt1)->GetCount(), 10);
  ASSERT_EQ(metrics.compression_format_metrics().counters.at(fmt2)->GetCount(), 0);

  metrics.mutable_compression_format_metrics()->IncrementCounter(fmt2, 20);
  ASSERT_EQ(metrics.compression_format_metrics().counters.at(fmt1)->GetCount(), 10);
  ASSERT_EQ(metrics.compression_format_metrics().counters.at(fmt2)->GetCount(), 20);

  metrics.mutable_compression_format_metrics()->IncrementCounter(fmt1, 10);
  ASSERT_EQ(metrics.compression_format_metrics().counters.at(fmt1)->GetCount(), 20);
  ASSERT_EQ(metrics.compression_format_metrics().counters.at(fmt2)->GetCount(), 20);

  ASSERT_TRUE(metrics.mutable_collector()->Flush());
  // Logger sees the counter increments after Flush().
  ASSERT_EQ(logger->counters().at(
                fs_metrics::CompressionFormatMetrics::MakeCompressionMetricOptions(source, fmt1)),
            20);
  ASSERT_EQ(logger->counters().at(
                fs_metrics::CompressionFormatMetrics::MakeCompressionMetricOptions(source, fmt2)),
            20);

  // No other counters were incremented.
  for (const auto fmt : formats) {
    if (fmt == fmt1 || fmt == fmt2) {
      continue;
    }
    ASSERT_EQ(logger->counters().at(
                  fs_metrics::CompressionFormatMetrics::MakeCompressionMetricOptions(source, fmt)),
              0);
  }

  // No pending increments.
  for (const auto fmt : formats) {
    ASSERT_EQ(metrics.compression_format_metrics().counters.at(fmt)->GetCount(), 0);
  }
}

}  // namespace
}  // namespace fs_metrics
