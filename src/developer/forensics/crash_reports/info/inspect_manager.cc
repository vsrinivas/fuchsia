// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/info/inspect_manager.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <map>
#include <utility>

#include "src/developer/forensics/crash_reports/constants.h"
#include "src/developer/forensics/crash_reports/errors.h"
#include "src/developer/forensics/utils/time.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/substitute.h"

namespace forensics {
namespace crash_reports {
namespace {

using files::JoinPath;
using inspect::Node;

std::string CurrentTime(timekeeper::Clock* clock) {
  auto current_time = CurrentUTCTime(clock);
  if (current_time.has_value()) {
    return current_time.value();
  } else {
    FX_LOGS(ERROR) << "Failed to get current UTC time";
    return "<unknown>";
  }
}

}  // namespace

InspectManager::Report::Report(const std::string& program_name,
                               const std::string& local_report_id) {
  path_ = JoinPath("/crash_reporter/reports",
                   JoinPath(InspectNodeManager::SanitizeString(program_name), local_report_id));
}

InspectManager::InspectManager(inspect::Node* root_node, timekeeper::Clock* clock)
    : node_manager_(root_node),
      clock_(clock),
      crash_register_stats_(&node_manager_, "/fidl/fuchsia.feedback.CrashReportingProductRegister"),
      crash_reporter_stats_(&node_manager_, "/fidl/fuchsia.feedback.CrashReporter") {
  node_manager_.Get("/config/crash_server");
  node_manager_.Get("/crash_reporter/queue");
  node_manager_.Get("/crash_reporter/reports");
  node_manager_.Get("/crash_reporter/settings");
}

bool InspectManager::AddReport(const std::string& program_name,
                               const std::string& local_report_id) {
  if (Contains(local_report_id)) {
    FX_LOGS(ERROR) << fxl::Substitute("Local report $0 already exposed in Inspect",
                                      local_report_id);
    return false;
  }

  reports_.emplace(local_report_id, Report(program_name, local_report_id));

  Report& report = reports_.at(local_report_id);
  inspect::Node& report_node = node_manager_.Get(report.Path());
  report.creation_time_ = report_node.CreateString("creation_time", CurrentTime(clock_));

  return true;
}

bool InspectManager::SetUploadAttempt(const std::string& local_report_id, uint64_t upload_attempt) {
  if (!Contains(local_report_id)) {
    FX_LOGS(ERROR) << "Failed to find local report " << local_report_id;
    return false;
  }

  Report& report = reports_.at(local_report_id);

  if (!report.upload_attempts_) {
    report.upload_attempts_ = node_manager_.Get(report.Path()).CreateUint("upload_attempts", 1u);
  } else {
    report.upload_attempts_.Set(upload_attempt);
  }

  return true;
}

bool InspectManager::MarkReportAsUploaded(const std::string& local_report_id,
                                          const std::string& server_properties_report_id) {
  if (!Contains(local_report_id)) {
    FX_LOGS(ERROR) << "Failed to find local report " << local_report_id;
    return false;
  }

  Report& report = reports_.at(local_report_id);
  report.final_state_ = node_manager_.Get(report.Path()).CreateString("final_state", "uploaded");

  const std::string server_path = JoinPath(report.Path(), "crash_server");

  inspect::Node& server = node_manager_.Get(server_path);

  report.server_id_ = server.CreateString("id", server_properties_report_id);
  report.server_creation_time_ = server.CreateString("creation_time", CurrentTime(clock_));

  return true;
}

bool InspectManager::MarkReportAsArchived(const std::string& local_report_id) {
  if (!Contains(local_report_id)) {
    FX_LOGS(ERROR) << "Failed to find local report " << local_report_id;
    return false;
  }

  Report& report = reports_.at(local_report_id);
  report.final_state_ = node_manager_.Get(report.Path()).CreateString("final_state", "archived");

  return true;
}

bool InspectManager::MarkReportAsGarbageCollected(const std::string& local_report_id) {
  if (!Contains(local_report_id)) {
    FX_LOGS(ERROR) << "Failed to find local report " << local_report_id;
    return false;
  }

  Report& report = reports_.at(local_report_id);
  report.final_state_ =
      node_manager_.Get(report.Path()).CreateString("final_state", "garbage_collected");

  return true;
}

void InspectManager::ExposeConfig(const crash_reports::Config& config) {
  auto* crash_server = &config_.crash_server;
  inspect::Node& server = node_manager_.Get("/config/crash_server");

  crash_server->upload_policy =
      server.CreateString(kCrashServerUploadPolicyKey, ToString(config.crash_server.upload_policy));
  if (config.crash_server.url) {
    crash_server->url = server.CreateString(kCrashServerUrlKey, *config.crash_server.url.get());
  }
}

void InspectManager::ExposeSettings(crash_reports::Settings* settings) {
  settings->RegisterUploadPolicyWatcher(
      [this](const crash_reports::Settings::UploadPolicy& upload_policy) {
        OnUploadPolicyChange(upload_policy);
      });
}

void InspectManager::SetQueueSize(const uint64_t size) {
  inspect::Node& queue = node_manager_.Get("/crash_reporter/queue");
  if (!queue_.size) {
    queue_.size = queue.CreateUint("size", size);
  } else {
    queue_.size.Set(size);
  }
}

void InspectManager::UpdateCrashRegisterProtocolStats(InspectProtocolStatsUpdateFn update) {
  std::invoke(update, crash_register_stats_);
}

void InspectManager::UpdateCrashReporterProtocolStats(InspectProtocolStatsUpdateFn update) {
  std::invoke(update, crash_reporter_stats_);
}

bool InspectManager::Contains(const std::string& local_report_id) {
  return reports_.find(local_report_id) != reports_.end();
}

void InspectManager::OnUploadPolicyChange(
    const crash_reports::Settings::UploadPolicy& upload_policy) {
  // |settings_.upload_policy| will change so we only create a StringProperty the first time it is
  // needed.
  if (!settings_.upload_policy) {
    settings_.upload_policy = node_manager_.Get("/crash_reporter/settings")
                                  .CreateString("upload_policy", ToString(upload_policy));
  } else {
    settings_.upload_policy.Set(ToString(upload_policy));
  }
}

void InspectManager::ExposeStore(const StorageSize max_size) {
  store_.max_size_in_kb = node_manager_.Get("/crash_reporter/store")
                              .CreateUint("max_size_in_kb", max_size.ToKilobytes());
}

void InspectManager::IncreaseReportsGarbageCollectedBy(uint64_t num_reports) {
  if (!store_.num_garbage_collected) {
    store_.num_garbage_collected = node_manager_.Get("/crash_reporter/store")
                                       .CreateUint("num_reports_garbage_collected", num_reports);
  } else {
    store_.num_garbage_collected.Add(num_reports);
  }
}

void InspectManager::UpsertComponentToProductMapping(const std::string& component_url,
                                                     const crash_reports::Product& product) {
  const std::string path =
      JoinPath("/crash_register/mappings", InspectNodeManager::SanitizeString(component_url));
  inspect::Node& node = node_manager_.Get(path);

  component_to_products_[component_url] = Product{
      .name = node.CreateString("name", product.name),
      .version = node.CreateString("version", product.version.HasValue()
                                                  ? product.version.Value()
                                                  : ToReason(product.version.Error())),
      .channel = node.CreateString("channel", product.channel.HasValue()
                                                  ? product.channel.Value()
                                                  : ToReason(product.channel.Error())),
  };
}

}  // namespace crash_reports
}  // namespace forensics
