// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/metrics.h>
#include <fs/metrics/registry.h>

#include <utility>

namespace fs {

namespace {

// Mirrors ids defined in cobalt metric definitions for Filesystems.
struct VnodeCobalt {
  // Maps a vnode operation to a metric_id in filesystem cobalt configuration.
  using MetricId = fs_metrics::Event;

  // Enum of Vnode related event codes.
  enum class EventCode : uint32_t {
    kUnknown = 0,
  };
};

const char* GetMetricName(uint32_t metric_id) {
  switch (static_cast<VnodeCobalt::MetricId>(metric_id)) {
    case VnodeCobalt::MetricId::kClose:
      return "Vnode.Close";
    case VnodeCobalt::MetricId::kRead:
      return "Vnode.Read";
    case VnodeCobalt::MetricId::kWrite:
      return "Vnode.Write";
    case VnodeCobalt::MetricId::kAppend:
      return "Vnode.Append";
    case VnodeCobalt::MetricId::kTruncate:
      return "Vnode.Truncate";
    case VnodeCobalt::MetricId::kSetAttr:
      return "Vnode.SetAttribute";
    case VnodeCobalt::MetricId::kGetAttr:
      return "Vnoode.GetAttribute";
    case VnodeCobalt::MetricId::kReadDir:
      return "Vnode.ReadDir";
    case VnodeCobalt::MetricId::kSync:
      return "Vnode.Sync";
    case VnodeCobalt::MetricId::kLookUp:
      return "Vnode.LookUp";
    case VnodeCobalt::MetricId::kCreate:
      return "Vnode.Create";
    case VnodeCobalt::MetricId::kUnlink:
      return "Vnode.Unlink";
    case VnodeCobalt::MetricId::kLink:
      return "Vnode.Link";
    default:
      return "kUnknown";
  };
}

// Default options for VnodeMetrics that are in tens of nanoseconds precision.
const cobalt_client::HistogramOptions kVnodeOptionsNanoOp =
    cobalt_client::HistogramOptions::Exponential(VnodeMetrics::kHistogramBuckets, 10 * (1024 - 1));

// Default options for VnodeMetrics that are in microseconds precision.
const cobalt_client::HistogramOptions kVnodeOptionsMicroOp =
    cobalt_client::HistogramOptions::Exponential(VnodeMetrics::kHistogramBuckets,
                                                 10000 * (1024 - 1));

cobalt_client::HistogramOptions MakeHistogramOptions(const cobalt_client::HistogramOptions& base,
                                                     VnodeCobalt::MetricId metric_id,
                                                     VnodeCobalt::EventCode event_code) {
  cobalt_client::HistogramOptions options = base;
  options.metric_id = static_cast<uint32_t>(metric_id);
  options.event_code = static_cast<uint32_t>(event_code);
  options.get_metric_name = GetMetricName;
  // TODO(gevalentino): Provide functions that translates event codes to names, for every metric,
  // or share an event_code enum for all metrics.
  options.get_event_name = nullptr;
  return options;
}

}  // namespace

VnodeMetrics::VnodeMetrics(cobalt_client::Collector* collector, const fbl::String& fs_name,
                           bool local_metrics) {
  // Initialize all the metrics for the collector.
  cobalt_client::HistogramOptions nano_base = kVnodeOptionsNanoOp;
  cobalt_client::HistogramOptions micro_base = kVnodeOptionsMicroOp;
  nano_base.component = fs_name;
  micro_base.component = fs_name;
  if (local_metrics) {
    nano_base.SetMode(cobalt_client::MetricOptions::Mode::kRemoteAndLocal);
    micro_base.SetMode(cobalt_client::MetricOptions::Mode::kRemoteAndLocal);
  } else {
    nano_base.SetMode(cobalt_client::MetricOptions::Mode::kRemote);
    micro_base.SetMode(cobalt_client::MetricOptions::Mode::kRemote);
  }

  close.Initialize(MakeHistogramOptions(nano_base, VnodeCobalt::MetricId::kClose,
                                        VnodeCobalt::EventCode::kUnknown),
                   collector);
  read.Initialize(MakeHistogramOptions(micro_base, VnodeCobalt::MetricId::kRead,
                                       VnodeCobalt::EventCode::kUnknown),
                  collector);
  write.Initialize(MakeHistogramOptions(micro_base, VnodeCobalt::MetricId::kWrite,
                                        VnodeCobalt::EventCode::kUnknown),
                   collector);
  append.Initialize(MakeHistogramOptions(micro_base, VnodeCobalt::MetricId::kAppend,
                                         VnodeCobalt::EventCode::kUnknown),
                    collector);
  truncate.Initialize(MakeHistogramOptions(micro_base, VnodeCobalt::MetricId::kTruncate,
                                           VnodeCobalt::EventCode::kUnknown),
                      collector);
  set_attr.Initialize(MakeHistogramOptions(micro_base, VnodeCobalt::MetricId::kSetAttr,
                                           VnodeCobalt::EventCode::kUnknown),
                      collector);
  get_attr.Initialize(MakeHistogramOptions(nano_base, VnodeCobalt::MetricId::kGetAttr,
                                           VnodeCobalt::EventCode::kUnknown),
                      collector);
  sync.Initialize(MakeHistogramOptions(micro_base, VnodeCobalt::MetricId::kSync,
                                       VnodeCobalt::EventCode::kUnknown),
                  collector);
  read_dir.Initialize(MakeHistogramOptions(micro_base, VnodeCobalt::MetricId::kReadDir,
                                           VnodeCobalt::EventCode::kUnknown),
                      collector);
  look_up.Initialize(MakeHistogramOptions(micro_base, VnodeCobalt::MetricId::kLookUp,
                                          VnodeCobalt::EventCode::kUnknown),
                     collector);
  create.Initialize(MakeHistogramOptions(micro_base, VnodeCobalt::MetricId::kCreate,
                                         VnodeCobalt::EventCode::kUnknown),
                    collector);
  unlink.Initialize(MakeHistogramOptions(micro_base, VnodeCobalt::MetricId::kUnlink,
                                         VnodeCobalt::EventCode::kUnknown),
                    collector);
  link.Initialize(MakeHistogramOptions(micro_base, VnodeCobalt::MetricId::kLink,
                                       VnodeCobalt::EventCode::kUnknown),
                  collector);
}

Metrics::Metrics(cobalt_client::CollectorOptions options, bool local_metrics,
                 const fbl::String& fs_name)
    : collector_(std::move(options)),
      vnode_metrics_(&collector_, fs_name, local_metrics),
      is_enabled_(false) {}

const VnodeMetrics& Metrics::vnode_metrics() const { return vnode_metrics_; }

VnodeMetrics* Metrics::mutable_vnode_metrics() { return &vnode_metrics_; }

void Metrics::EnableMetrics(bool should_enable) {
  is_enabled_ = should_enable;
  vnode_metrics_.metrics_enabled = should_enable;
}

bool Metrics::IsEnabled() const { return is_enabled_; }

}  // namespace fs
