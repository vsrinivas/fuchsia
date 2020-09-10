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

#include <abs_clock/clock.h>

#include "src/cobalt/bin/system-metrics/log_stats_fetcher.h"

namespace cobalt {

class LogStatsFetcherImpl : public LogStatsFetcher {
 public:
  static std::unordered_map<std::string, ComponentEventCode> LoadAllowlist(
      const std::string& allowlist_path);

  // This constructor creates an ArchiveAccessor using |context| and loads the allowlist from the
  // config file (located in kDefaultAllowlistFilePath).
  LogStatsFetcherImpl(async_dispatcher_t* dispatcher, sys::ComponentContext* context);

  // This constructor is intended to be used in tests to inject fake dependencies.
  LogStatsFetcherImpl(async_dispatcher_t* dispatcher,
                      fit::function<fuchsia::diagnostics::ArchiveAccessorPtr()> connector,
                      std::unordered_map<std::string, ComponentEventCode> component_code_map,
                      abs_clock::Clock* clock);

  // Overridden from LogStatsFetcher:
  void FetchMetrics(MetricsCallback metrics_callback) override;

 private:
  void OnInspectSnapshotReady(const std::vector<inspect::contrib::DiagnosticsData>& data_vector);

  // Calculate total error count across all components. Returns false if inspect data is invalid.
  bool CalculateTotalErrorCount(const inspect::contrib::DiagnosticsData& inspect, uint64_t* result);

  // Calculates error count for each component since the last report. Components not in the
  // allowlist will be lumped together and reported as "Other". Returns false if inspect data is
  // invalid.
  bool CalculatePerComponentErrorCount(const inspect::contrib::DiagnosticsData& inspect,
                                       uint64_t total_count,
                                       std::unordered_map<ComponentEventCode, uint64_t>* result);

  // Calculates the number of logs in klog since the last report. Return false if inspect data is
  // invalid.
  bool CalculateKlogCount(const inspect::contrib::DiagnosticsData& inspect, uint64_t* result);

  // Extracts the granular log stats from inspect and populates a vector of GranularLogStatsRecords.
  // Each record contains the file path and line number of the location that an error log was
  // generated.
  bool PopulateGranularStats(const inspect::contrib::DiagnosticsData& inspect,
                             std::vector<GranularStatsRecord>* granular_stats);

  uint64_t last_reported_total_error_count_ = 0;
  uint64_t last_reported_klog_count_ = 0;
  int64_t last_reported_bucket_id_ = -1;
  MetricsCallback metrics_callback_;
  async::Executor executor_;
  inspect::contrib::ArchiveReader archive_reader_;

  // A map from component urls to cobalt event codes for all components in the allowlist.
  std::unordered_map<std::string, ComponentEventCode> component_code_map_;

  // Map from component event codes (as defined in metrics.yaml) to the last known error count
  std::unordered_map<ComponentEventCode, uint64_t> per_component_error_count_;

  abs_clock::Clock* const clock_;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_SYSTEM_METRICS_LOG_STATS_FETCHER_IMPL_H_
