// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_SYSTEM_METRICS_LOG_STATS_FETCHER_IMPL_H_
#define SRC_COBALT_BIN_SYSTEM_METRICS_LOG_STATS_FETCHER_IMPL_H_

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <lib/inspect/contrib/cpp/archive_reader.h>
#include <lib/sys/cpp/component_context.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "src/cobalt/bin/system-metrics/log_stats_fetcher.h"

namespace cobalt {

class LogStatsFetcherImpl : public LogStatsFetcher {
 public:
  static std::unordered_map<std::string, ComponentEventCode> LoadAllowlist(
      const std::string& allowlist_path);

  // This constructor creates an ArchiveAccessor using |context| and loads the allowlist from the
  // config file (located in kDefaultAllowlistFilePath).
  LogStatsFetcherImpl(async_dispatcher_t* dispatcher, sys::ComponentContext* context);

  // This constructor is intended to be used in tests that need a fake ArchiveAccessor and
  // allowlist.
  LogStatsFetcherImpl(async_dispatcher_t* dispatcher,
                      fit::function<fuchsia::diagnostics::ArchiveAccessorPtr()> connector,
                      std::unordered_map<std::string, ComponentEventCode> component_code_map);

  // Overridden from LogStatsFetcher:
  void FetchMetrics(MetricsCallback metrics_callback) override;

 private:
  void OnInspectSnapshotReady(const std::vector<inspect::contrib::DiagnosticsData>& data_vector);

  uint64_t last_reported_error_count_ = 0;
  uint64_t last_reported_klog_count_ = 0;
  MetricsCallback metrics_callback_;
  async::Executor executor_;
  inspect::contrib::ArchiveReader archive_reader_;

  // A map from component urls to cobalt event codes for all components in the allowlist.
  std::unordered_map<std::string, ComponentEventCode> component_code_map_;

  // Map from component event codes (as defined in metrics.yaml) to the last known error count
  std::unordered_map<ComponentEventCode, uint64_t> per_component_error_count_;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_SYSTEM_METRICS_LOG_STATS_FETCHER_IMPL_H_
