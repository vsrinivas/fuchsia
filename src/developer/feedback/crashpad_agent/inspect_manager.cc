// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/inspect_manager.h"

#include <map>
#include <utility>

#include "src/lib/fxl/logging.h"

namespace fuchsia {
namespace crash {

namespace {

// Returns a (non-localized) human-readable time stamp.
std::string GetCurrentTimeString() {
  char buffer[32];
  time_t now = time(nullptr);
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %X %Z", localtime(&now));
  return std::string(buffer);
}

}  // namespace

InspectManager::Report::Report(::inspect_deprecated::Node* parent_node,
                               const crashpad::UUID& local_report_id) {
  node_ = parent_node->CreateChild(local_report_id.ToString());
  creation_time_ = node_.CreateStringProperty("creation_time", GetCurrentTimeString());
}

void InspectManager::Report::MarkUploaded(std::string server_id) {
  server_node_ = node_.CreateChild("crash_server");
  server_id_ = server_node_.CreateStringProperty("id", std::move(server_id));
  server_creation_time_ =
      server_node_.CreateStringProperty("creation_time", GetCurrentTimeString());
}

InspectManager::InspectManager(::inspect_deprecated::Node* root_node) : root_node_(root_node) {}

InspectManager::Report* InspectManager::AddReport(const std::string& program_name,
                                                  const crashpad::UUID& local_report_id) {
  // Find or create a Node for this program.
  InspectManager::ReportList* report_list;
  auto it = report_lists_.find(program_name);
  if (it != report_lists_.end()) {
    report_list = &it->second;
  } else {
    report_lists_[program_name].node = root_node_->CreateChild(program_name);
    report_list = &report_lists_[program_name];
  }

  // Create a new Report object and return it.
  report_list->reports.push_back(Report(&report_list->node, local_report_id));
  return &report_list->reports.back();
}

}  // namespace crash
}  // namespace fuchsia
