// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/inspect_manager.h"

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
  settings_.node = root_node_->CreateChild(kInspectSettingsName);
  reports_.node = root_node_->CreateChild(kInspectReportsName);
}

bool InspectManager::AddReport(const std::string& program_name,
                               const std::string& local_report_id) {
  if (Contains(local_report_id)) {
    FX_LOGS(ERROR) << fxl::Substitute("Local crash report, ID $0, already exposed in Inspect",
                                      local_report_id);
    return false;
  }

  if (reports_.program_nodes.find(program_name) == reports_.program_nodes.end()) {
    reports_.program_nodes[program_name] = reports_.node.CreateChild(program_name);
  }

  reports_.reports.emplace(local_report_id, Report(&reports_.program_nodes[program_name],
                                                   local_report_id, CurrentTime()));
  return true;
}

bool InspectManager::MarkReportAsUploaded(const std::string& local_report_id,
                                          const std::string& server_report_id) {
  if (Contains(local_report_id)) {
    reports_.reports.at(local_report_id).MarkAsUploaded(server_report_id, CurrentTime());
    return true;
  }
  FX_LOGS(ERROR) << "Failed to find local crash report, ID " << local_report_id;
  return false;
}

void InspectManager::ExposeConfig(const feedback::Config& config) {
  auto* crashpad_database = &config_.crashpad_database;
  auto* crash_server = &config_.crash_server;

  crashpad_database->node = config_.node.CreateChild(kCrashpadDatabaseKey);
  crashpad_database->max_size_in_kb = crashpad_database->node.CreateUint(
      kCrashpadDatabaseMaxSizeInKbKey, config.crashpad_database.max_size_in_kb);

  crash_server->node = config_.node.CreateChild(kCrashServerKey);
  crash_server->upload_policy = crash_server->node.CreateString(
      kCrashServerUploadPolicyKey, ToString(config.crash_server.upload_policy));

  if (config.crash_server.url) {
    crash_server->url =
        crash_server->node.CreateString(kCrashServerUrlKey, *config.crash_server.url.get());
  }
}

void InspectManager::ExposeSettings(feedback::Settings* settings) {
  settings->RegisterUploadPolicyWatcher(
      [this](const feedback::Settings::UploadPolicy& upload_policy) {
        OnUploadPolicyChange(upload_policy);
      });
}

bool InspectManager::Contains(const std::string& local_report_id) {
  return reports_.reports.find(local_report_id) != reports_.reports.end();
}
void InspectManager::OnUploadPolicyChange(const feedback::Settings::UploadPolicy& upload_policy) {
  settings_.upload_policy = settings_.node.CreateString("upload_policy", ToString(upload_policy));
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
