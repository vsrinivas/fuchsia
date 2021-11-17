// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_APP_DIAGNOSTICS_IMPL_H_
#define SRC_COBALT_BIN_APP_DIAGNOSTICS_IMPL_H_

#include <lib/inspect/cpp/inspect.h>

#include "src/lib/fxl/macros.h"
#include "third_party/cobalt/src/public/diagnostics_interface.h"
#include "third_party/cobalt/src/public/lib/report_spec.h"

namespace cobalt {

// Implementation of the Diagnostics Interface for receiving diagnostic information about the
// functioning of the Cobalt Core library.
//
// In Fuchsia, the diagnostic information is published to Inspect.
class DiagnosticsImpl : public DiagnosticsInterface {
 public:
  explicit DiagnosticsImpl(inspect::Node node);
  ~DiagnosticsImpl() override = default;

  void SentObservationResult(const Status& status) override;

  void ObservationStoreUpdated(const std::map<lib::ReportSpec, uint64_t>& num_obs_per_report,
                               int64_t store_byte_count, int64_t max_store_bytes) override;

  void LoggerCalled(int perProjectLoggerCallsMadeMetricDimensionLoggerMethod,
                    const std::string& project) override;

  void TrackDiskUsage(int storageClass, int64_t bytes, int64_t byte_limit) override;

 private:
  // Logger calls inspect storage for a single project or method in the Inspect hierarchy.
  struct LoggerCalls {
    inspect::Node node;
    inspect::IntProperty num_calls;
    inspect::IntProperty last_successful_time;

    LoggerCalls(inspect::Node* parent_node, const std::string& node_name)
        : node(parent_node->CreateChild(node_name)),
          num_calls(node.CreateInt("num_calls", 0)),
          last_successful_time(node.CreateInt("last_successful_time", 0)) {}
  };

  LoggerCalls* FindOrCreateLoggerCallsForProject(const std::string& project);
  LoggerCalls* FindOrCreateLoggerCallsForMethod(
      int perProjectLoggerCallsMadeMetricDimensionLoggerMethod);

  // Disk usage inspect storage for a single storage class in the Inspect hierarchy.
  struct DiskUsage {
    inspect::Node node;
    inspect::IntProperty current_bytes;
    int64_t max_bytes;
    inspect::IntProperty max_bytes_property;
    inspect::IntProperty byte_limit;

    DiskUsage(inspect::Node* parent_node, int storageClass)
        : node(parent_node->CreateChild("storage_class_" + std::to_string(storageClass))),
          current_bytes(node.CreateInt("current_bytes", 0)),
          max_bytes(0),
          max_bytes_property(node.CreateInt("max_bytes", 0)),
          byte_limit(node.CreateInt("byte_limit", 0)) {}
  };

  DiskUsage* FindOrCreateDiskUsage(int storageClass);

  // Root inspect node to create all child nodes under.
  inspect::Node node_;

  // Inspect data for sending observations to Clearcut.
  inspect::Node send_observations_;
  inspect::IntProperty send_observations_successes_;
  inspect::IntProperty send_observations_errors_;
  inspect::IntProperty last_successful_send_time_;
  inspect::IntProperty last_send_error_time_;
  inspect::IntProperty last_send_error_code_;
  inspect::StringProperty last_send_error_message_;
  inspect::StringProperty last_send_error_details_;

  // Inspect data for stored observations.
  inspect::Node stored_observations_;
  inspect::IntProperty stored_observations_total_;
  inspect::IntProperty stored_observations_byte_count_;
  inspect::IntProperty stored_observations_byte_count_limit_;
  std::map<std::string, inspect::IntProperty> stored_observations_per_report_;

  // Inspect data for internal Cobalt metrics.
  inspect::Node internal_metrics_;
  inspect::Node logger_calls_;
  inspect::IntProperty total_logger_calls_;
  inspect::IntProperty last_successful_logger_call_time_;
  inspect::Node logger_calls_per_project_node_;
  std::map<std::string, std::unique_ptr<LoggerCalls>> logger_calls_per_project_;
  std::mutex logger_calls_per_project_lock_;
  inspect::Node logger_calls_per_method_node_;
  std::map<int, std::unique_ptr<LoggerCalls>> logger_calls_per_method_;
  std::mutex logger_calls_per_method_lock_;
  inspect::Node disk_usage_;
  inspect::Node disk_usage_per_storage_class_node_;
  std::map<int, std::unique_ptr<DiskUsage>> disk_usage_per_storage_class_;
  std::mutex disk_usage_per_storage_class_lock_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DiagnosticsImpl);
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_APP_DIAGNOSTICS_IMPL_H_
