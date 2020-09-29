// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>
#include <vector>

#include <fs/metrics/cobalt_metrics.h>
#include <fs/metrics/events.h>

namespace fs_metrics {

namespace {

// Mirrors ids defined in cobalt metric definitions for Filesystems.
struct FsCommonCobalt {
  // Enum of FsCommonMetrics related event codes.
  enum class EventCode : uint32_t {
    kUnknown = 0,
  };
};

// Default options for FsCommonMetrics that are in tens of nanoseconds precision.
const cobalt_client::HistogramOptions kFsCommonOptionsNanoOp =
    cobalt_client::HistogramOptions::Exponential(FsCommonMetrics::kHistogramBuckets,
                                                 10 * (1024 - 1));

// Default options for FsCommonMetrics that are in microseconds precision.
const cobalt_client::HistogramOptions kFsCommonOptionsMicroOp =
    cobalt_client::HistogramOptions::Exponential(FsCommonMetrics::kHistogramBuckets,
                                                 10000 * (1024 - 1));

cobalt_client::HistogramOptions MakeHistogramOptions(const cobalt_client::HistogramOptions& base,
                                                     Event metric_id,
                                                     FsCommonCobalt::EventCode event_code) {
  cobalt_client::HistogramOptions options = base;
  options.metric_id = static_cast<uint32_t>(metric_id);
  for (auto& event_code : options.event_codes) {
    event_code = 0;
  }
  return options;
}

}  // namespace

FsCommonMetrics::FsCommonMetrics(cobalt_client::Collector* collector, const fbl::String& fs_name) {
  // Initialize all the metrics for the collector.
  cobalt_client::HistogramOptions nano_base = kFsCommonOptionsNanoOp;
  cobalt_client::HistogramOptions micro_base = kFsCommonOptionsMicroOp;
  nano_base.component = fs_name.c_str();
  micro_base.component = fs_name.c_str();

  vnode.close.Initialize(
      MakeHistogramOptions(nano_base, Event::kClose, FsCommonCobalt::EventCode::kUnknown),
      collector);
  vnode.read.Initialize(
      MakeHistogramOptions(micro_base, Event::kRead, FsCommonCobalt::EventCode::kUnknown),
      collector);
  vnode.write.Initialize(
      MakeHistogramOptions(micro_base, Event::kWrite, FsCommonCobalt::EventCode::kUnknown),
      collector);
  vnode.append.Initialize(
      MakeHistogramOptions(micro_base, Event::kAppend, FsCommonCobalt::EventCode::kUnknown),
      collector);
  vnode.truncate.Initialize(
      MakeHistogramOptions(micro_base, Event::kTruncate, FsCommonCobalt::EventCode::kUnknown),
      collector);
  vnode.set_attr.Initialize(
      MakeHistogramOptions(micro_base, Event::kSetAttr, FsCommonCobalt::EventCode::kUnknown),
      collector);
  vnode.get_attr.Initialize(
      MakeHistogramOptions(nano_base, Event::kGetAttr, FsCommonCobalt::EventCode::kUnknown),
      collector);
  vnode.sync.Initialize(
      MakeHistogramOptions(micro_base, Event::kSync, FsCommonCobalt::EventCode::kUnknown),
      collector);
  vnode.read_dir.Initialize(
      MakeHistogramOptions(micro_base, Event::kReadDir, FsCommonCobalt::EventCode::kUnknown),
      collector);
  vnode.look_up.Initialize(
      MakeHistogramOptions(micro_base, Event::kLookUp, FsCommonCobalt::EventCode::kUnknown),
      collector);
  vnode.create.Initialize(
      MakeHistogramOptions(micro_base, Event::kCreate, FsCommonCobalt::EventCode::kUnknown),
      collector);
  vnode.unlink.Initialize(
      MakeHistogramOptions(micro_base, Event::kUnlink, FsCommonCobalt::EventCode::kUnknown),
      collector);
  vnode.link.Initialize(
      MakeHistogramOptions(micro_base, Event::kLink, FsCommonCobalt::EventCode::kUnknown),
      collector);
  journal.write_data.Initialize(MakeHistogramOptions(micro_base, Event::kJournalWriteData,
                                                     FsCommonCobalt::EventCode::kUnknown),
                                collector);
  journal.write_metadata.Initialize(MakeHistogramOptions(micro_base, Event::kJournalWriteMetadata,
                                                         FsCommonCobalt::EventCode::kUnknown),
                                    collector);
  journal.trim_data.Initialize(MakeHistogramOptions(micro_base, Event::kJournalTrimData,
                                                    FsCommonCobalt::EventCode::kUnknown),
                               collector);
  journal.sync.Initialize(
      MakeHistogramOptions(micro_base, Event::kJournalSync, FsCommonCobalt::EventCode::kUnknown),
      collector);
  journal.schedule_task.Initialize(MakeHistogramOptions(micro_base, Event::kJournalScheduleTask,
                                                        FsCommonCobalt::EventCode::kUnknown),
                                   collector);
  journal.writer_write_data.Initialize(
      MakeHistogramOptions(micro_base, Event::kJournalWriterWriteData,
                           FsCommonCobalt::EventCode::kUnknown),
      collector);
  journal.writer_write_metadata.Initialize(
      MakeHistogramOptions(micro_base, Event::kJournalWriterWriteMetadata,
                           FsCommonCobalt::EventCode::kUnknown),
      collector);
  journal.writer_trim_data.Initialize(
      MakeHistogramOptions(micro_base, Event::kJournalWriterTrimData,
                           FsCommonCobalt::EventCode::kUnknown),
      collector);
  journal.writer_sync.Initialize(MakeHistogramOptions(micro_base, Event::kJournalWriterSync,
                                                      FsCommonCobalt::EventCode::kUnknown),
                                 collector);

  journal.writer_write_info_block.Initialize(
      MakeHistogramOptions(micro_base, Event::kJournalWriterWriteInfoBlock,
                           FsCommonCobalt::EventCode::kUnknown),
      collector);
}

CompressionFormatMetrics::CompressionFormatMetrics(
    cobalt_client::Collector* collector, fs_metrics::CompressionSource compression_source) {
  source = compression_source;
  if (compression_source == fs_metrics::CompressionSource::kUnknown) {
    return;
  }
  std::vector<fs_metrics::CompressionFormat> formats = {
      fs_metrics::CompressionFormat::kUnknown,
      fs_metrics::CompressionFormat::kUncompressed,
      fs_metrics::CompressionFormat::kCompressedLZ4,
      fs_metrics::CompressionFormat::kCompressedZSTD,
      fs_metrics::CompressionFormat::kCompressedZSTDSeekable,
      fs_metrics::CompressionFormat::kCompressedZSTDChunked,
  };
  for (auto format : formats) {
    counters.emplace(format, std::make_unique<cobalt_client::Counter>(
                                 MakeCompressionMetricOptions(source, format), collector));
  }
}

cobalt_client::MetricOptions CompressionFormatMetrics::MakeCompressionMetricOptions(
    fs_metrics::CompressionSource source, fs_metrics::CompressionFormat format) {
  cobalt_client::MetricOptions options = {};
  options.metric_id =
      static_cast<std::underlying_type<fs_metrics::Event>::type>(fs_metrics::Event::kCompression);
  options.event_codes = {0};
  options.metric_dimensions = 2;
  options.event_codes[0] = static_cast<uint32_t>(source);
  options.event_codes[1] = static_cast<uint32_t>(format);
  return options;
}

void CompressionFormatMetrics::IncrementCounter(fs_metrics::CompressionFormat format,
                                                uint64_t size) {
  if (counters.find(format) == counters.end()) {
    return;
  }
  counters[format]->Increment(size);
}

Metrics::Metrics(std::unique_ptr<cobalt_client::Collector> collector, const fbl::String& fs_name,
                 fs_metrics::CompressionSource source)
    : collector_(std::move(collector)),
      fs_common_metrics_(collector_.get(), fs_name),
      compression_format_metrics_(collector_.get(), source) {}

const FsCommonMetrics& Metrics::fs_common_metrics() const { return fs_common_metrics_; }

FsCommonMetrics* Metrics::mutable_fs_common_metrics() { return &fs_common_metrics_; }

const CompressionFormatMetrics& Metrics::compression_format_metrics() const {
  return compression_format_metrics_;
}

CompressionFormatMetrics* Metrics::mutable_compression_format_metrics() {
  return &compression_format_metrics_;
}

void Metrics::EnableMetrics(bool should_enable) {
  is_enabled_ = should_enable;
  fs_common_metrics_.metrics_enabled = should_enable;
}

bool Metrics::IsEnabled() const { return is_enabled_; }

}  // namespace fs_metrics
