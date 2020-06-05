// Copyright 2020  The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/system-metrics/log_stats_fetcher_impl.h"

#include <lib/syslog/cpp/macros.h>

namespace cobalt {

LogStatsFetcherImpl::LogStatsFetcherImpl(async_dispatcher_t* dispatcher,
                                         sys::ComponentContext* context)
    : executor_(dispatcher),
      archive_reader_(context->svc()->Connect<fuchsia::diagnostics::ArchiveAccessor>(),
                      {"archivist.cmx:root/log_stats:error_logs"}) {}

void LogStatsFetcherImpl::FetchMetrics(MetricsCallback metrics_callback) {
  metrics_callback_ = std::move(metrics_callback);
  auto promise = archive_reader_.GetInspectSnapshot()
                     .and_then([this](std::vector<inspect::contrib::DiagnosticsData>& data_vector) {
                       this->OnInspectSnapshotReady(data_vector);
                     })
                     .or_else([](std::string& error) {
                       FX_LOGS(WARNING) << "Error while fetching log stats: " << error;
                     });
  executor_.schedule_task(std::move(promise));
}

void LogStatsFetcherImpl::OnInspectSnapshotReady(
    const std::vector<inspect::contrib::DiagnosticsData>& data_vector) {
  if (data_vector.size() != 1u) {
    FX_LOGS(ERROR) << "Expected 1 archive, received " << data_vector.size();
    return;
  }

  const rapidjson::Value& value = data_vector[0].GetByPath({"root", "log_stats", "error_logs"});

  if (!value.IsInt()) {
    FX_LOGS(ERROR) << "error_logs doesn't exist or is not an integer";
    return;
  }

  LogStatsFetcher::Metrics metrics;
  metrics.error_count = value.GetInt() - last_reported_error_count_;

  FX_LOGS(DEBUG) << "Current error count: " << value.GetInt()
                 << ", since last report: " << metrics.error_count;

  if (metrics_callback_(metrics)) {
    last_reported_error_count_ = value.GetInt();
  }
}

}  // namespace cobalt
