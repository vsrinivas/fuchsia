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
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/substitute.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace {

using files::JoinPath;
using inspect::Node;

}  // namespace

InspectManager::InspectManager(inspect::Node* root_node, timekeeper::Clock* clock)
    : node_manager_(root_node), clock_(clock) {
  node_manager_.GetOrDie("/settings");
  node_manager_.GetOrDie("/reports");
  node_manager_.GetOrDie("/config/crashpad_database");
  node_manager_.GetOrDie("/config/crash_server");
}

bool InspectManager::AddReport(const std::string& program_name,
                               const std::string& local_report_id) {
  if (Contains(local_report_id)) {
    FX_LOGS(ERROR) << fxl::Substitute("Local crash report, ID $0, already exposed in Inspect",
                                      local_report_id);
    return false;
  }

  const std::string report_path = JoinPath("/reports", JoinPath(program_name, local_report_id));

  reports_.emplace(local_report_id, report_path);
  reports_.at(local_report_id).creation_time_ =
      node_manager_.GetOrDie(report_path)->CreateString("creation_time", CurrentTime());

  return true;
}

bool InspectManager::MarkReportAsUploaded(const std::string& local_report_id,
                                          const std::string& server_properties_report_id) {
  if (!Contains(local_report_id)) {
    FX_LOGS(ERROR) << "Failed to find local crash report, ID " << local_report_id;
    return false;
  }

  Report& report = reports_.at(local_report_id);
  const std::string server_path = JoinPath(report.Path(), "crash_server");

  inspect::Node* server = node_manager_.GetOrDie(server_path);

  report.server_id_ = server->CreateString("id", server_properties_report_id);
  report.server_creation_time_ = server->CreateString("creation_time", CurrentTime());

  return true;
}

void InspectManager::ExposeConfig(const feedback::Config& config) {
  auto* crashpad_database = &config_.crashpad_database;
  auto* crash_server = &config_.crash_server;

  inspect::Node* server = node_manager_.GetOrDie("/config/crash_server");

  crashpad_database->max_size_in_kb =
      node_manager_.GetOrDie("/config/crashpad_database")
          ->CreateUint(kCrashpadDatabaseMaxSizeInKbKey, config.crashpad_database.max_size_in_kb);

  crash_server->upload_policy = server->CreateString(kCrashServerUploadPolicyKey,
                                                     ToString(config.crash_server.upload_policy));
  if (config.crash_server.url) {
    crash_server->url = server->CreateString(kCrashServerUrlKey, *config.crash_server.url.get());
  }
}

void InspectManager::ExposeSettings(feedback::Settings* settings) {
  settings->RegisterUploadPolicyWatcher(
      [this](const feedback::Settings::UploadPolicy& upload_policy) {
        OnUploadPolicyChange(upload_policy);
      });
}

bool InspectManager::Contains(const std::string& local_report_id) {
  return reports_.find(local_report_id) != reports_.end();
}

void InspectManager::OnUploadPolicyChange(const feedback::Settings::UploadPolicy& upload_policy) {
  // |settings_.upload_policy| will change so we only create a StringProperty the first time it is
  // needed.
  if (!settings_.upload_policy) {
    settings_.upload_policy =
        node_manager_.GetOrDie("/settings")->CreateString("upload_policy", ToString(upload_policy));
  } else {
    settings_.upload_policy.Set(ToString(upload_policy));
  }
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
