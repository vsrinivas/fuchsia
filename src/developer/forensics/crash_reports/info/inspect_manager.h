// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_INFO_INSPECT_MANAGER_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_INFO_INSPECT_MANAGER_H_

#include <lib/inspect/cpp/vmo/types.h>
#include <lib/timekeeper/clock.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "src/developer/forensics/crash_reports/config.h"
#include "src/developer/forensics/crash_reports/product.h"
#include "src/developer/forensics/crash_reports/settings.h"
#include "src/developer/forensics/utils/inspect_node_manager.h"
#include "src/developer/forensics/utils/inspect_protocol_stats.h"
#include "src/developer/forensics/utils/storage_size.h"
#include "src/lib/fxl/macros.h"

namespace forensics {
namespace crash_reports {

// Encapsulates the global state exposed through Inspect.
class InspectManager {
 public:
  InspectManager(inspect::Node* root_node, timekeeper::Clock* clock);

  // Exposes the static configuration of the crash reporter.
  void ExposeConfig(const crash_reports::Config& config);

  // Exposes the mutable settings of the crash reporter.
  void ExposeSettings(crash_reports::Settings* settings);

  // Exposes the static properties of the report store.
  void ExposeStore(StorageSize max_size);

  // Records the current size of the queue of pending reports.
  void SetQueueSize(uint64_t size);

  // Updates stats related to fuchsia.feedback.CrashReportingProductRegister.
  void UpdateCrashRegisterProtocolStats(InspectProtocolStatsUpdateFn update);

  // Updates stats related to fuchsia.feedback.CrashReporter.
  void UpdateCrashReporterProtocolStats(InspectProtocolStatsUpdateFn update);

  // Upserts the mapping component URL to Product that a client registered.
  void UpsertComponentToProductMapping(const std::string& component_url, const Product& product);

  // Increase the total number of garbage collected reports by |num_reports|.
  void IncreaseReportsGarbageCollectedBy(uint64_t num_reports);

  // Adds a new report under the given program.
  //
  // Returns false if there is already a report with |local_report_id| as ID (for the given program
  // or another).
  // TODO(fxbug.dev/57903) Revisit this when refactoring Queue related Inspect.
  bool AddReport(const std::string& program_name, const std::string& local_report_id);

  // Sets the number of upload attempts for an existing report.
  //
  // Returns false if there are no reports with |local_report_id| as ID.
  // TODO(fxbug.dev/57903) Revisit this when refactoring Queue related Inspect.
  bool SetUploadAttempt(const std::string& local_report_id, uint64_t upload_attempt);

  // Marks an existing report as uploaded, storing its server report ID.
  //
  // Returns false if there are no reports with |local_report_id| as ID.
  // TODO(fxbug.dev/57903) Revisit this when refactoring Queue related Inspect.
  bool MarkReportAsUploaded(const std::string& local_report_id,
                            const std::string& server_report_id);

  // Mark an existing report as archived.
  //
  // Returns false if there are no reports with |local_report_id| as ID.
  // TODO(fxbug.dev/57903) Revisit this when refactoring Queue related Inspect.
  bool MarkReportAsArchived(const std::string& local_report_id);

  // Mark an existing report as garbage collected.
  //
  // Returns false if there are no report with |local_report_id| as ID.
  // TODO(fxbug.dev/57903) Revisit this when refactoring Queue related Inspect.
  bool MarkReportAsGarbageCollected(const std::string& local_report_id);

 private:
  bool Contains(const std::string& local_report_id);

  // Callback to update |settings_| on upload policy changes.
  void OnUploadPolicyChange(const crash_reports::Settings::UploadPolicy& upload_policy);

  // Inspect node containing the static configuration.
  struct Config {
    // Inspect node containing the crash server configuration.
    struct CrashServerConfig {
      inspect::StringProperty upload_policy;
      inspect::StringProperty url;
    };

    CrashServerConfig crash_server;
  };

  // Inspect node containing the mutable settings.
  struct Settings {
    inspect::StringProperty upload_policy;
  };

  // Inspect node containing the store properties.
  struct Store {
    inspect::UintProperty max_size_in_kb;
    inspect::UintProperty num_garbage_collected;
  };

  // Inspect node containing the queue properties.
  // TODO(fxbug.dev/57903) Revisit this when refactoring Queue related Inspect.
  struct Queue {
    inspect::UintProperty size;
  };

  // Inspect node for a single report.
  // TODO(fxbug.dev/57903) Revisit this when refactoring Queue related Inspect.
  struct Report {
    Report(const std::string& program_name, const std::string& local_report_id);

    const std::string& Path() { return path_; }

    inspect::StringProperty creation_time_;
    inspect::UintProperty upload_attempts_;
    inspect::StringProperty final_state_;

    inspect::StringProperty server_id_;
    inspect::StringProperty server_creation_time_;

   private:
    // A |Report|'s path is its location relative to the root Inspect node in the Inspect tree.
    //
    // E.g., "/reports/$program_name/$local_report_id"
    //
    // Backslashes in $program_name are replaced with (char)0x07, the ASCII bell character.
    std ::string path_;
  };

  // Inspect node for a single product.
  struct Product {
    inspect::StringProperty name;
    inspect::StringProperty version;
    inspect::StringProperty channel;
  };

  InspectNodeManager node_manager_;
  timekeeper::Clock* clock_;

  Config config_;
  Settings settings_;
  Store store_;
  Queue queue_;
  InspectProtocolStats crash_register_stats_;
  InspectProtocolStats crash_reporter_stats_;

  // Maps a local report ID to a |Report|.
  std::map<std::string, Report> reports_;

  // Maps a component URL to a |Product|.
  std::map<std::string, Product> component_to_products_;

  FXL_DISALLOW_COPY_AND_ASSIGN(InspectManager);
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_INFO_INSPECT_MANAGER_H_
