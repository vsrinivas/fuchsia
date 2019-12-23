// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_INSPECT_MANAGER_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_INSPECT_MANAGER_H_

#include <lib/inspect/cpp/vmo/types.h>
#include <lib/timekeeper/clock.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "src/developer/feedback/crashpad_agent/config.h"
#include "src/developer/feedback/crashpad_agent/settings.h"
#include "src/developer/feedback/utils/inspect_node_manager.h"
#include "src/lib/fxl/macros.h"

namespace feedback {

// Encapsulates the global state exposed through Inspect.
class InspectManager {
 public:
  InspectManager(inspect::Node* root_node, timekeeper::Clock* clock);

  // Exposes the static configuration of the crash reporter.
  void ExposeConfig(const feedback::Config& config);

  // Exposes the mutable settings of the crash reporter.
  void ExposeSettings(feedback::Settings* settings);

  // Exposes the static properties of the crash report database.
  void ExposeDatabase(uint64_t max_crashpad_database_size_in_kb);

  // Adds a new report under the given program.
  //
  // Returns false if there is already a report with |local_report_id| as ID (for the given program
  // or another).
  bool AddReport(const std::string& program_name, const std::string& local_report_id);

  // Sets the number of upload attempts for an existing report.
  //
  // Returns false if there are no reports with |local_report_id| as ID.
  bool SetUploadAttempt(const std::string& local_report_id, uint64_t upload_attempt);

  // Marks an existing report as uploaded, storing its server report ID.
  //
  // Returns false if there are no reports with |local_report_id| as ID.
  bool MarkReportAsUploaded(const std::string& local_report_id,
                            const std::string& server_report_id);

  // Mark an existing report as archived.
  //
  // Returns false if there are no reports with |local_report_id| as ID.
  bool MarkReportAsArchived(const std::string& local_report_id);

  // Mark an existing report as garbage collected.
  //
  // Returns false if there are no report with |local_report_id| as ID.
  bool MarkReportAsGarbageCollected(const std::string& local_report_id);

 private:
  bool Contains(const std::string& local_report_id);

  // Callback to update |settings_| on upload policy changes.
  void OnUploadPolicyChange(const feedback::Settings::UploadPolicy& upload_policy);

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

  // Inspect node containing the database properties.
  struct Database {
    inspect::UintProperty max_crashpad_database_size_in_kb;
  };

  // Inspect node for a single report.
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

  InspectNodeManager node_manager_;
  timekeeper::Clock* clock_;
  Config config_;
  Settings settings_;
  Database database_;
  // Maps a local report ID to a |Report|.
  std::map<std::string, Report> reports_;

  FXL_DISALLOW_COPY_AND_ASSIGN(InspectManager);
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_INSPECT_MANAGER_H_
