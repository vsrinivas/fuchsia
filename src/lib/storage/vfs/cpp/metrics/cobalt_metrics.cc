// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/metrics/cobalt_metrics.h"

#include <memory>
#include <utility>
#include <vector>

#include "src/lib/storage/vfs/cpp/metrics/events.h"

namespace fs_metrics {

namespace {

// Default options for FsCommonMetrics that are in tens of nanoseconds precision.
const cobalt_client::HistogramOptions kFsCommonOptionsNanoOp =
    cobalt_client::HistogramOptions::Exponential(FsCommonMetrics::kHistogramBuckets,
                                                 10 * (1024 - 1));

// Default options for FsCommonMetrics that are in microseconds precision.
const cobalt_client::HistogramOptions kFsCommonOptionsMicroOp =
    cobalt_client::HistogramOptions::Exponential(FsCommonMetrics::kHistogramBuckets,
                                                 10000 * (1024 - 1));

cobalt_client::HistogramOptions MakeHistogramOptions(const cobalt_client::HistogramOptions& base,
                                                     Event metric_id) {
  cobalt_client::HistogramOptions options = base;
  options.metric_id = static_cast<uint32_t>(metric_id);
  return options;
}

}  // namespace

FsCommonMetrics::FsCommonMetrics(cobalt_client::Collector* collector, Source source) {
  // Initialize all the metrics for the collector.
  cobalt_client::HistogramOptions nano_base = kFsCommonOptionsNanoOp;
  cobalt_client::HistogramOptions micro_base = kFsCommonOptionsMicroOp;
  uint32_t source_event_code = static_cast<uint32_t>(source);
  nano_base.metric_dimensions = 1;
  nano_base.event_codes[0] = source_event_code;
  micro_base.metric_dimensions = 1;
  micro_base.event_codes[0] = source_event_code;

  vnode.close.Initialize(MakeHistogramOptions(nano_base, Event::kClose), collector);
  vnode.read.Initialize(MakeHistogramOptions(micro_base, Event::kRead), collector);
  vnode.write.Initialize(MakeHistogramOptions(micro_base, Event::kWrite), collector);
  vnode.append.Initialize(MakeHistogramOptions(micro_base, Event::kAppend), collector);
  vnode.truncate.Initialize(MakeHistogramOptions(micro_base, Event::kTruncate), collector);
  vnode.set_attr.Initialize(MakeHistogramOptions(micro_base, Event::kSetAttr), collector);
  vnode.get_attr.Initialize(MakeHistogramOptions(nano_base, Event::kGetAttr), collector);
  vnode.sync.Initialize(MakeHistogramOptions(micro_base, Event::kSync), collector);
  vnode.read_dir.Initialize(MakeHistogramOptions(micro_base, Event::kReadDir), collector);
  vnode.look_up.Initialize(MakeHistogramOptions(micro_base, Event::kLookUp), collector);
  vnode.create.Initialize(MakeHistogramOptions(micro_base, Event::kCreate), collector);
  vnode.unlink.Initialize(MakeHistogramOptions(micro_base, Event::kUnlink), collector);
  vnode.link.Initialize(MakeHistogramOptions(micro_base, Event::kLink), collector);
  journal.write_data.Initialize(MakeHistogramOptions(micro_base, Event::kJournalWriteData),
                                collector);
  journal.write_metadata.Initialize(MakeHistogramOptions(micro_base, Event::kJournalWriteMetadata),
                                    collector);
  journal.trim_data.Initialize(MakeHistogramOptions(micro_base, Event::kJournalTrimData),
                               collector);
  journal.sync.Initialize(MakeHistogramOptions(micro_base, Event::kJournalSync), collector);
  journal.schedule_task.Initialize(MakeHistogramOptions(micro_base, Event::kJournalScheduleTask),
                                   collector);
  journal.writer_write_data.Initialize(
      MakeHistogramOptions(micro_base, Event::kJournalWriterWriteData), collector);
  journal.writer_write_metadata.Initialize(
      MakeHistogramOptions(micro_base, Event::kJournalWriterWriteMetadata), collector);
  journal.writer_trim_data.Initialize(
      MakeHistogramOptions(micro_base, Event::kJournalWriterTrimData), collector);
  journal.writer_sync.Initialize(MakeHistogramOptions(micro_base, Event::kJournalWriterSync),
                                 collector);

  journal.writer_write_info_block.Initialize(
      MakeHistogramOptions(micro_base, Event::kJournalWriterWriteInfoBlock), collector);

  fragmentation_metrics.extents_per_file.Initialize(
      MakeHistogramOptions(nano_base, Event::kFragmentationExtentsPerFile), collector);
  fragmentation_metrics.in_use_fragments.Initialize(
      MakeHistogramOptions(nano_base, Event::kFragmentationInUseFragments), collector);
  fragmentation_metrics.free_fragments.Initialize(
      MakeHistogramOptions(nano_base, Event::kFragmentationFreeFragments), collector);

  cobalt_client::MetricOptions options = {
      .metric_id = static_cast<uint32_t>(Event::kFragmentationTotalNodes),
      .metric_dimensions = 1,
      .event_codes = {source_event_code},
  };
  fragmentation_metrics.total_nodes.Initialize(options, collector);

  options.metric_id = static_cast<uint32_t>(Event::kFragmentationInodesInUse);
  fragmentation_metrics.inodes_in_use.Initialize(options, collector);

  options.metric_id = static_cast<uint32_t>(Event::kFragmentationExtentContainersInUse);
  fragmentation_metrics.extent_containers_in_use.Initialize(options, collector);
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

Metrics::Metrics(std::unique_ptr<cobalt_client::Collector> collector, Source source,
                 CompressionSource compression_source)
    : source_(source),
      collector_(std::move(collector)),
      fs_common_metrics_(collector_.get(), source),
      compression_format_metrics_(collector_.get(), compression_source) {}

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

void Metrics::RecordOldestVersionMounted(std::string_view version) {
  std::scoped_lock lock(mutex_);
  // We hack the version into the component field (which is the only dimension that supports a
  // string value), whilst we store the real storage sub-component in a dimension.  There is
  // precedent for this kind of hack; SWD do something similar.
  cobalt_client::MetricOptions options{
      .component = std::string(version),
      .metric_id = static_cast<uint32_t>(Event::kVersion),
      .metric_dimensions = 1,
      .event_codes = {static_cast<uint32_t>(source_)},
  };
  auto iter = temporary_counters_.find(options);
  if (iter == temporary_counters_.end()) {
    iter =
        temporary_counters_
            .insert(std::make_unique<cobalt_client::Counter>(std::move(options), collector_.get()))
            .first;
  }
  (*iter)->Increment();
}

bool Metrics::Flush() {
  std::scoped_lock lock(mutex_);
  if (collector_->Flush()) {
    // The counters are low frequency, so after flushing, it's likely that they won't get used
    // again, so we can jettison them.
    temporary_counters_.clear();
    return true;
  }
  return false;
}

}  // namespace fs_metrics
