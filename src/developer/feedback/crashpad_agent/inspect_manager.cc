// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/inspect_manager.h"

#include <map>
#include <utility>

#include "src/developer/feedback/crashpad_agent/constants.h"
#include "src/lib/fxl/logging.h"

namespace feedback {

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

InspectManager::InspectManager(::inspect_deprecated::Node* root_node) : root_node_(root_node) {
  config_.node = root_node->CreateChild(kInspectConfigName);
  crash_reports_.node = root_node_->CreateChild(kInspectReportsName);
}

InspectManager::Report* InspectManager::AddReport(const std::string& program_name,
                                                  const crashpad::UUID& local_report_id) {
  // Find or create a Node for this program.
  InspectManager::ReportList* report_list;
  auto* report_lists = &crash_reports_.report_lists;
  auto it = report_lists->find(program_name);
  if (it != report_lists->end()) {
    report_list = &it->second;
  } else {
    (*report_lists)[program_name].node = crash_reports_.node.CreateChild(program_name);
    report_list = &(*report_lists)[program_name];
  }

  // Create a new Report object and return it.
  report_list->reports.push_back(Report(&report_list->node, local_report_id));
  return &report_list->reports.back();
}

void InspectManager::ExposeConfig(const feedback::Config& config) {
  auto* crashpad_database = &config_.crashpad_database;
  auto* crash_server = &config_.crash_server;

  crashpad_database->node = config_.node.CreateChild(kCrashpadDatabaseKey);
  crashpad_database->path = config_.crashpad_database.node.CreateStringProperty(
      kCrashpadDatabasePathKey, config.crashpad_database.path);
  crashpad_database->max_size_in_kb = crashpad_database->node.CreateUIntMetric(
      kCrashpadDatabaseMaxSizeInKbKey, config.crashpad_database.max_size_in_kb);

  crash_server->node = config_.node.CreateChild(kCrashServerKey);
  crash_server->enable_upload = crash_server->node.CreateStringProperty(
      kCrashServerEnableUploadKey, (config.crash_server.enable_upload ? "true" : "false"));

  if (config.crash_server.enable_upload) {
    crash_server->url =
        crash_server->node.CreateStringProperty(kCrashServerUrlKey, *config.crash_server.url.get());
  }

  config_.feedback_data_collection_timeout_in_milliseconds =
      config_.node.CreateUIntMetric(kFeedbackDataCollectionTimeoutInMillisecondsKey,
                                    config.feedback_data_collection_timeout.to_msecs());
}

}  // namespace feedback
