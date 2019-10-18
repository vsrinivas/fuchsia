// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/crashpad_agent.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fit/result.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "src/developer/feedback/crashpad_agent/config.h"
#include "src/developer/feedback/crashpad_agent/crash_server.h"
#include "src/developer/feedback/crashpad_agent/feedback_data_provider_ptr.h"
#include "src/developer/feedback/crashpad_agent/report_util.h"
#include "src/developer/feedback/crashpad_agent/scoped_unlink.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace {

using crashpad::CrashReportDatabase;
using fuchsia::feedback::CrashReport;
using fuchsia::feedback::Data;

const char kDefaultConfigPath[] = "/pkg/data/default_config.json";
const char kOverrideConfigPath[] = "/config/data/override_config.json";

// This should be kept higher than the timeout the component serving fuchsia.feedback.DataProvider
// has on its side for each feedback data as we pay the price for making the request (establishing
// the connection, potentially spawning the serving component for the first time, getting the
// response, etc.) .
constexpr zx::duration kFeedbackDataCollectionTimeout = zx::sec(10) + /*some slack*/ zx::sec(1);

}  // namespace

std::unique_ptr<CrashpadAgent> CrashpadAgent::TryCreate(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
    InspectManager* inspect_manager) {
  Config config;

  // We use the default config included in the package of this component if no override config was
  // specified or if we failed to parse the override config.
  bool use_default_config = true;

  if (files::IsFile(kOverrideConfigPath)) {
    use_default_config = false;
    if (const zx_status_t status = ParseConfig(kOverrideConfigPath, &config); status != ZX_OK) {
      // We failed to parse the override config: fall back to the default config.
      use_default_config = true;
      FX_PLOGS(ERROR, status) << "Failed to read override config file at " << kOverrideConfigPath
                              << " - falling back to default config file";
    }
  }

  // Either there was no override config or we failed to parse it.
  if (use_default_config) {
    if (const zx_status_t status = ParseConfig(kDefaultConfigPath, &config); status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to read default config file at " << kDefaultConfigPath;

      FX_LOGS(FATAL) << "Failed to set up crash analyzer";
      return nullptr;
    }
  }

  return CrashpadAgent::TryCreate(dispatcher, std::move(services), std::move(config),
                                  inspect_manager);
}

std::unique_ptr<CrashpadAgent> CrashpadAgent::TryCreate(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services, Config config,
    InspectManager* inspect_manager) {
  std::unique_ptr<CrashServer> crash_server;
  if (config.crash_server.url) {
    crash_server = std::make_unique<CrashServer>(*config.crash_server.url);
  }
  return CrashpadAgent::TryCreate(dispatcher, std::move(services), std::move(config),
                                  std::move(crash_server), inspect_manager);
}

std::unique_ptr<CrashpadAgent> CrashpadAgent::TryCreate(
    async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services, Config config,
    std::unique_ptr<CrashServer> crash_server, InspectManager* inspect_manager) {
  auto database = Database::TryCreate(config.crashpad_database);
  if (!database) {
    FX_LOGS(FATAL) << "Failed to set up crash analyzer";
    return nullptr;
  }

  return std::unique_ptr<CrashpadAgent>(
      new CrashpadAgent(dispatcher, std::move(services), std::move(config), std::move(database),
                        std::move(crash_server), inspect_manager));
}

CrashpadAgent::CrashpadAgent(async_dispatcher_t* dispatcher,
                             std::shared_ptr<sys::ServiceDirectory> services, Config config,
                             std::unique_ptr<Database> database,
                             std::unique_ptr<CrashServer> crash_server,
                             InspectManager* inspect_manager)
    : dispatcher_(dispatcher),
      executor_(dispatcher),
      services_(services),
      config_(std::move(config)),
      database_(std::move(database)),
      crash_server_(std::move(crash_server)),
      inspect_manager_(inspect_manager) {
  FXL_DCHECK(dispatcher_);
  FXL_DCHECK(services_);
  FXL_DCHECK(database_);
  FXL_DCHECK(inspect_manager_);
  if (config.crash_server.url) {
    FXL_DCHECK(crash_server_);
  }

  // TODO(fxb/6360): use PrivacySettingsWatcher if upload_policy is READ_FROM_PRIVACY_SETTINGS.
  settings_.set_upload_policy(config_.crash_server.upload_policy);

  inspect_manager_->ExposeConfig(config_);
  inspect_manager_->ExposeSettings(&settings_);
}

void CrashpadAgent::File(fuchsia::feedback::CrashReport report, FileCallback callback) {
  if (!report.has_program_name()) {
    FX_LOGS(ERROR) << "Invalid crash report. No program name. Won't file.";
    callback(fit::error(ZX_ERR_INVALID_ARGS));
    return;
  }
  FX_LOGS(INFO) << "Generating crash report for " << report.program_name();

  auto promise =
      GetFeedbackData(dispatcher_, services_, kFeedbackDataCollectionTimeout)
          .then([this, report = std::move(report)](
                    fit::result<Data>& result) mutable -> fit::result<crashpad::UUID> {
            Data feedback_data;
            if (result.is_ok()) {
              feedback_data = result.take_value();
            }

            const std::string program_name = report.program_name();

            std::map<std::string, std::string> annotations;
            std::map<std::string, fuchsia::mem::Buffer> attachments;
            std::optional<fuchsia::mem::Buffer> minidump;
            BuildAnnotationsAndAttachments(std::move(report), std::move(feedback_data),
                                           &annotations, &attachments, &minidump);

            crashpad::UUID local_report_id;
            if (!database_->MakeNewReport(attachments, minidump, annotations, &local_report_id)) {
              FX_LOGS(ERROR) << "Error making new report";
              return fit::error();
            }

            inspect_manager_->AddReport(program_name, local_report_id.ToString());

            return fit::ok(std::move(local_report_id));
          })
          .then([callback = std::move(callback), this](fit::result<crashpad::UUID>& result) {
            if (result.is_error()) {
              FX_LOGS(ERROR) << "Failed to file crash report. Won't retry.";
              callback(fit::error(ZX_ERR_INTERNAL));
            } else {
              callback(fit::ok());
              const auto& local_report_id = result.value();
              UploadReport(local_report_id);
            }

            database_->GarbageCollect();
          });

  executor_.schedule_task(std::move(promise));
}

bool CrashpadAgent::UploadReport(const crashpad::UUID& local_report_id) {
  if (settings_.upload_policy() == Settings::UploadPolicy::DISABLED) {
    FX_LOGS(INFO) << fxl::StringPrintf(
        "Upload to remote crash server disabled. Local crash report, ID %s, available under %s",
        local_report_id.ToString().c_str(), database_->path());
    if (!database_->Archive(local_report_id)) {
      FX_LOGS(ERROR) << "Error archiving local report " << local_report_id.ToString();
    }
    return true;
  } else if (settings_.upload_policy() == Settings::UploadPolicy::LIMBO) {
    // TODO(fxb/6049): put the limbo crash reports in the pending queue.
    return true;
  }

  // Read local crash report as an "upload" report.
  auto upload_report = database_->GetUploadReport(local_report_id);
  if (!upload_report) {
    FX_LOGS(ERROR) << "Error getting upload report for local report id "
                   << local_report_id.ToString();
    return false;
  }

  std::string server_report_id;
  if (!crash_server_->MakeRequest(upload_report->GetAnnotations(), upload_report->GetAttachments(),
                                  &server_report_id)) {
    FX_LOGS(ERROR) << "Error uploading local crash report, ID " << local_report_id.ToString();
    upload_report.reset();  // Release the report's lockfile.
    if (!database_->Archive(local_report_id)) {
      FX_LOGS(ERROR) << fxl::StringPrintf(
          "Error marking local report %s as having too many upload attempts",
          local_report_id.ToString().c_str());
    }
    return false;
  }
  FX_LOGS(INFO) << "Successfully uploaded crash report at https://crash.corp.google.com/"
                << server_report_id;
  if (!database_->MarkAsUploaded(std::move(upload_report), server_report_id)) {
    FX_LOGS(ERROR) << fxl::StringPrintf("Error marking local report %s as uploaded",
                                        local_report_id.ToString().c_str());
  }
  inspect_manager_->MarkReportAsUploaded(local_report_id.ToString(), server_report_id);

  return true;
}

}  // namespace feedback
