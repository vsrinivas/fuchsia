// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/metrics/cobalt_metrics.h>

#include <memory>
#include <utility>

#include <fs/metrics/events.h>

namespace fs_metrics {

namespace {

// Mirrors ids defined in cobalt metric definitions for Filesystems.
struct VnodeCobalt {
  // Enum of Vnode related event codes.
  enum class EventCode : uint32_t {
    kUnknown = 0,
  };
};

// Default options for VnodeMetrics that are in tens of nanoseconds precision.
const cobalt_client::HistogramOptions kVnodeOptionsNanoOp =
    cobalt_client::HistogramOptions::Exponential(VnodeMetrics::kHistogramBuckets, 10 * (1024 - 1));

// Default options for VnodeMetrics that are in microseconds precision.
const cobalt_client::HistogramOptions kVnodeOptionsMicroOp =
    cobalt_client::HistogramOptions::Exponential(VnodeMetrics::kHistogramBuckets,
                                                 10000 * (1024 - 1));

cobalt_client::HistogramOptions MakeHistogramOptions(const cobalt_client::HistogramOptions& base,
                                                     Event metric_id,
                                                     VnodeCobalt::EventCode event_code) {
  cobalt_client::HistogramOptions options = base;
  options.metric_id = static_cast<uint32_t>(metric_id);
  for (auto& event_code : options.event_codes) {
    event_code = 0;
  }
  return options;
}

}  // namespace

VnodeMetrics::VnodeMetrics(cobalt_client::Collector* collector, const fbl::String& fs_name) {
  // Initialize all the metrics for the collector.
  cobalt_client::HistogramOptions nano_base = kVnodeOptionsNanoOp;
  cobalt_client::HistogramOptions micro_base = kVnodeOptionsMicroOp;
  nano_base.component = fs_name.c_str();
  micro_base.component = fs_name.c_str();

  close.Initialize(MakeHistogramOptions(nano_base, Event::kClose, VnodeCobalt::EventCode::kUnknown),
                   collector);
  read.Initialize(MakeHistogramOptions(micro_base, Event::kRead, VnodeCobalt::EventCode::kUnknown),
                  collector);
  write.Initialize(
      MakeHistogramOptions(micro_base, Event::kWrite, VnodeCobalt::EventCode::kUnknown), collector);
  append.Initialize(
      MakeHistogramOptions(micro_base, Event::kAppend, VnodeCobalt::EventCode::kUnknown),
      collector);
  truncate.Initialize(
      MakeHistogramOptions(micro_base, Event::kTruncate, VnodeCobalt::EventCode::kUnknown),
      collector);
  set_attr.Initialize(
      MakeHistogramOptions(micro_base, Event::kSetAttr, VnodeCobalt::EventCode::kUnknown),
      collector);
  get_attr.Initialize(
      MakeHistogramOptions(nano_base, Event::kGetAttr, VnodeCobalt::EventCode::kUnknown),
      collector);
  sync.Initialize(MakeHistogramOptions(micro_base, Event::kSync, VnodeCobalt::EventCode::kUnknown),
                  collector);
  read_dir.Initialize(
      MakeHistogramOptions(micro_base, Event::kReadDir, VnodeCobalt::EventCode::kUnknown),
      collector);
  look_up.Initialize(
      MakeHistogramOptions(micro_base, Event::kLookUp, VnodeCobalt::EventCode::kUnknown),
      collector);
  create.Initialize(
      MakeHistogramOptions(micro_base, Event::kCreate, VnodeCobalt::EventCode::kUnknown),
      collector);
  unlink.Initialize(
      MakeHistogramOptions(micro_base, Event::kUnlink, VnodeCobalt::EventCode::kUnknown),
      collector);
  link.Initialize(MakeHistogramOptions(micro_base, Event::kLink, VnodeCobalt::EventCode::kUnknown),
                  collector);
}

Metrics::Metrics(std::unique_ptr<cobalt_client::Collector> collector, const fbl::String& fs_name)
    : collector_(std::move(collector)),
      vnode_metrics_(collector_.get(), fs_name),
      is_enabled_(false) {}

const VnodeMetrics& Metrics::vnode_metrics() const { return vnode_metrics_; }

VnodeMetrics* Metrics::mutable_vnode_metrics() { return &vnode_metrics_; }

void Metrics::EnableMetrics(bool should_enable) {
  is_enabled_ = should_enable;
  vnode_metrics_.metrics_enabled = should_enable;
}

bool Metrics::IsEnabled() const { return is_enabled_; }

}  // namespace fs_metrics
