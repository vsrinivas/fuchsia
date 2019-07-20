// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_INSPECT_MANAGER_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_INSPECT_MANAGER_H_

#include <lib/fit/function.h>
#include <lib/inspect_deprecated/component.h>

#include <map>
#include <string>
#include <vector>

#include "src/lib/fxl/macros.h"
#include "third_party/crashpad/util/misc/uuid.h"

namespace fuchsia {
namespace crash {

// Encapsulates the global state exposed through Inspect.
class InspectManager {
 public:
  // Inspect node for a single crash report.
  struct Report {
    Report(::inspect_deprecated::Node* parent_node, const crashpad::UUID& local_report_id);
    Report(Report&&) = default;

    // Add the |crash_server| entry after receiving a server response.
    void MarkUploaded(std::string server_id);

   private:
    ::inspect_deprecated::Node node_;
    ::inspect_deprecated::StringProperty creation_time_;

    ::inspect_deprecated::Node server_node_;
    ::inspect_deprecated::StringProperty server_id_;
    ::inspect_deprecated::StringProperty server_creation_time_;

    FXL_DISALLOW_COPY_AND_ASSIGN(Report);
  };

  InspectManager(::inspect_deprecated::Node* root_node);

  // Adds a new entry to the crash-list of a program, and returns it.
  Report* AddReport(const std::string& program_name, const crashpad::UUID& local_report_id);

 private:
  // Inspect node containing a list of reports.
  struct ReportList {
    ::inspect_deprecated::Node node;
    std::vector<Report> reports;
  };

  ::inspect_deprecated::Node* root_node_ = nullptr;
  // Maps program names to a list of |Report| nodes.
  std::map<std::string, ReportList> report_lists_;

  FXL_DISALLOW_COPY_AND_ASSIGN(InspectManager);
};

}  // namespace crash
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_INSPECT_MANAGER_H_
