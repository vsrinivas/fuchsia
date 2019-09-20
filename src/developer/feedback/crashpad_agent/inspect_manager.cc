// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/inspect_manager.h"

#include <lib/syslog/cpp/logger.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <ctime>
#include <map>
#include <utility>

#include "src/developer/feedback/crashpad_agent/constants.h"
#include "src/developer/feedback/crashpad_agent/settings.h"
#include "src/lib/fxl/strings/substitute.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

InspectManager::Report::Report(inspect::Node* parent_node, const std::string& local_report_id,
                               const std::string& creation_time) {
  node_ = parent_node->CreateChild(local_report_id);
  creation_time_ = node_.CreateString("creation_time", creation_time);
}

void InspectManager::Report::MarkAsUploaded(const std::string& server_report_id,
                                            const std::string& creation_time) {
  server_node_ = node_.CreateChild("crash_server");
  server_id_ = server_node_.CreateString("id", server_report_id);
  server_creation_time_ = server_node_.CreateString("creation_time", creation_time);
}

InspectManager::InspectManager(inspect::Node* root_node, timekeeper::Clock* clock)
    : root_node_(root_node), clock_(clock) {
  config_.node = root_node->CreateChild(kInspectConfigName);
  reports_.node = root_node_->CreateChild(kInspectReportsName);
}

bool InspectManager::AddReport(const std::string& program_name,
                               const std::string& local_report_id) {
  if (reports_.local_report_id_to_report.find(local_report_id) !=
      reports_.local_report_id_to_report.end()) {
    FX_LOGS(ERROR) << fxl::Substitute("Local crash report, ID $0, already exposed in Inspect",
                                      local_report_id);
    return false;
  }

  // Find or create a Node for this program.
  InspectManager::ReportList* report_list;
  auto* report_lists = &reports_.program_name_to_report_lists;
  if (auto it = report_lists->find(program_name); it != report_lists->end()) {
    report_list = &it->second;
  } else {
    (*report_lists)[program_name].node = reports_.node.CreateChild(program_name);
    report_list = &(*report_lists)[program_name];
  }

  // Create a new Report object and index it.
  report_list->reports.push_back(Report(&report_list->node, local_report_id, CurrentTime()));
  reports_.local_report_id_to_report[local_report_id] = &report_list->reports.back();
  return true;
}

bool InspectManager::MarkReportAsUploaded(const std::string& local_report_id,
                                          const std::string& server_report_id) {
  if (auto it = reports_.local_report_id_to_report.find(local_report_id);
      it != reports_.local_report_id_to_report.end()) {
    it->second->MarkAsUploaded(server_report_id, CurrentTime());
    return true;
  }
  FX_LOGS(ERROR) << "Failed to find local crash report, ID " << local_report_id;
  return false;
}

void InspectManager::ExposeConfig(const feedback::Config& config) {
  auto* crashpad_database = &config_.crashpad_database;
  auto* crash_server = &config_.crash_server;

  crashpad_database->node = config_.node.CreateChild(kCrashpadDatabaseKey);
  crashpad_database->path = config_.crashpad_database.node.CreateString(
      kCrashpadDatabasePathKey, config.crashpad_database.path);
  crashpad_database->max_size_in_kb = crashpad_database->node.CreateUint(
      kCrashpadDatabaseMaxSizeInKbKey, config.crashpad_database.max_size_in_kb);

  crash_server->node = config_.node.CreateChild(kCrashServerKey);
  crash_server->upload_policy = crash_server->node.CreateString(
      kCrashServerUploadPolicyKey, ToString(config.crash_server.upload_policy));

  if (config.crash_server.url) {
    crash_server->url =
        crash_server->node.CreateString(kCrashServerUrlKey, *config.crash_server.url.get());
  }

  config_.feedback_data_collection_timeout_in_milliseconds =
      config_.node.CreateUint(kFeedbackDataCollectionTimeoutInMillisecondsKey,
                              config.feedback_data_collection_timeout.to_msecs());
}

std::string InspectManager::CurrentTime() {
  zx::time_utc now_utc;
  if (const zx_status_t status = clock_->Now(&now_utc); status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to get current UTC time";
    return "<unknown>";
  }
  // std::gmtime expects epoch in seconds.
  const int64_t now_utc_seconds = now_utc.get() / zx::sec(1).get();
  char buffer[32];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %X %Z", std::gmtime(&now_utc_seconds));
  return std::string(buffer);
}

}  // namespace feedback
