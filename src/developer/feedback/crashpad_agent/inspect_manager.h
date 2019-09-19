// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_INSPECT_MANAGER_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_INSPECT_MANAGER_H_

#include <lib/fit/function.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/sys/inspect/cpp/component.h>

#include <map>
#include <string>
#include <vector>

#include "src/developer/feedback/crashpad_agent/config.h"
#include "src/lib/fxl/macros.h"

namespace feedback {

// Encapsulates the global state exposed through Inspect.
class InspectManager {
 public:
  // Inspect node for a single report.
  struct Report {
    Report(inspect::Node* parent_node, const std::string& local_report_id);
    Report(Report&&) = default;

    // Add the |crash_server| entry after receiving a server response.
    void MarkAsUploaded(std::string server_report_id);

   private:
    inspect::Node node_;
    inspect::StringProperty creation_time_;

    inspect::Node server_node_;
    inspect::StringProperty server_id_;
    inspect::StringProperty server_creation_time_;

    FXL_DISALLOW_COPY_AND_ASSIGN(Report);
  };

  InspectManager(inspect::Node* root_node);

  // Adds a new entry to the report-list of a program, and returns it.
  Report* AddReport(const std::string& program_name, const std::string& local_report_id);

  // Records the configuration file of the CrashpadAgent.
  void ExposeConfig(const feedback::Config& config);

 private:
  // Inspect node containing a list of reports.
  struct ReportList {
    inspect::Node node;
    std::vector<Report> reports;
  };

  // Inspect node containing the configuration file.
  struct Config {
    // Inspect node containing the database configuration.
    struct CrashpadDatabaseConfig {
      inspect::Node node;
      inspect::StringProperty path;
      inspect::UintProperty max_size_in_kb;
    };

    // Inspect node containing the crash server configuration.
    struct CrashServerConfig {
      inspect::Node node;
      inspect::StringProperty upload_policy;
      inspect::StringProperty url;
    };

    inspect::Node node;

    CrashpadDatabaseConfig crashpad_database;
    CrashServerConfig crash_server;
    inspect::UintProperty feedback_data_collection_timeout_in_milliseconds;
  };

  // Inspect node pointing to the list of reports.
  struct Reports {
    inspect::Node node;
    // Maps program names to a list of |Report| nodes.
    std::map<std::string, ReportList> report_lists;
  };

  inspect::Node* root_node_ = nullptr;

  Config config_;
  Reports crash_reports_;

  FXL_DISALLOW_COPY_AND_ASSIGN(InspectManager);
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_INSPECT_MANAGER_H_
