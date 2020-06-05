// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_SYSTEM_METRICS_LOG_STATS_FETCHER_IMPL_H_
#define SRC_COBALT_BIN_SYSTEM_METRICS_LOG_STATS_FETCHER_IMPL_H_

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <lib/inspect/contrib/cpp/archive_reader.h>
#include <lib/sys/cpp/component_context.h>

#include <string>
#include <vector>

#include "src/cobalt/bin/system-metrics/log_stats_fetcher.h"

namespace cobalt {

class LogStatsFetcherImpl : public LogStatsFetcher {
 public:
  LogStatsFetcherImpl(async_dispatcher_t* dispatcher, sys::ComponentContext* context);

  // Overridden from LogStatsFetcher:
  void FetchMetrics(MetricsCallback metrics_callback) override;

 private:
  void OnInspectSnapshotReady(const std::vector<inspect::contrib::DiagnosticsData>& data_vector);

  uint32_t last_reported_error_count_ = 0;
  MetricsCallback metrics_callback_;
  async::Executor executor_;
  inspect::contrib::ArchiveReader archive_reader_;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_SYSTEM_METRICS_LOG_STATS_FETCHER_IMPL_H_
