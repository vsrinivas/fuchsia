// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/crashpad_agent.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fit/result.h>
#include <lib/syslog/cpp/logger.h>
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
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "third_party/crashpad/client/crash_report_database.h"
#include "third_party/crashpad/client/prune_crash_reports.h"
#include "third_party/crashpad/util/misc/metrics.h"
#include "third_party/crashpad/util/misc/uuid.h"

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
  if (!files::IsDirectory(config.crashpad_database.path)) {
    files::CreateDirectory(config.crashpad_database.path);
  }

  std::unique_ptr<crashpad::CrashReportDatabase> database(
      crashpad::CrashReportDatabase::Initialize(base::FilePath(config.crashpad_database.path)));
  if (!database) {
    FX_LOGS(ERROR) << "error initializing local crash report database at "
                   << config.crashpad_database.path;
    FX_LOGS(FATAL) << "failed to set up crash analyzer";
    return nullptr;
  }

  return std::unique_ptr<CrashpadAgent>(
      new CrashpadAgent(dispatcher, std::move(services), std::move(config), std::move(database),
                        std::move(crash_server), inspect_manager));
}

CrashpadAgent::CrashpadAgent(async_dispatcher_t* dispatcher,
                             std::shared_ptr<sys::ServiceDirectory> services, Config config,
                             std::unique_ptr<crashpad::CrashReportDatabase> database,
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

namespace {

// TODO(fxb/6442): turn this helper into one of the public methods of the Database wrapper.
bool MakeNewReport(crashpad::CrashReportDatabase* database,
                   const std::map<std::string, fuchsia::mem::Buffer>& attachments,
                   const std::optional<fuchsia::mem::Buffer>& minidump,
                   crashpad::UUID* local_report_id) {
  // Create local Crashpad report.
  std::unique_ptr<crashpad::CrashReportDatabase::NewReport> report;
  if (const auto status = database->PrepareNewCrashReport(&report);
      status != crashpad::CrashReportDatabase::kNoError) {
    FX_LOGS(ERROR) << "Error creating local Crashpad report (" << status << ")";
    return false;
  }

  // Write attachments.
  for (const auto& [filename, content] : attachments) {
    AddAttachment(filename, content, report.get());
  }

  // Optionally write minidump.
  if (minidump.has_value()) {
    if (!WriteVMO(minidump.value(), report->Writer())) {
      FX_LOGS(WARNING) << "error attaching minidump to Crashpad report";
    }
  }

  // Finish new local Crashpad report.
  if (const auto status = database->FinishedWritingCrashReport(std::move(report), local_report_id);
      status != crashpad::CrashReportDatabase::kNoError) {
    FX_LOGS(ERROR) << "error writing local Crashpad report (" << status << ")";
    return false;
  }

  return true;
}

}  // namespace

void CrashpadAgent::File(fuchsia::feedback::CrashReport report, FileCallback callback) {
  if (!report.has_program_name()) {
    FX_LOGS(ERROR) << "Invalid crash report. No program name. Won't file.";
    callback(fit::error(ZX_ERR_INVALID_ARGS));
    return;
  }
  FX_LOGS(INFO) << "generating crash report for " << report.program_name();

  using UploadArgs = std::tuple<crashpad::UUID, std::map<std::string, std::string>, bool>;

  auto promise =
      GetFeedbackData(dispatcher_, services_, kFeedbackDataCollectionTimeout)
          .then([this, report = std::move(report)](
                    fit::result<Data>& result) mutable -> fit::result<UploadArgs> {
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
            if (!MakeNewReport(database_.get(), attachments, minidump, &local_report_id)) {
              return fit::error();
            }

            inspect_manager_->AddReport(program_name, local_report_id.ToString());

            return fit::ok(std::make_tuple(std::move(local_report_id), std::move(annotations),
                                           minidump.has_value()));
          })
          .then([callback = std::move(callback), this](fit::result<UploadArgs>& result) {
            if (result.is_error()) {
              FX_LOGS(ERROR) << "Failed to file crash report. Won't retry.";
              callback(fit::error(ZX_ERR_INTERNAL));
            } else {
              callback(fit::ok());
              const auto& args = result.value();
              UploadReport(std::get<0>(args), std::get<1>(args), std::get<2>(args));
            }

            PruneDatabase();
            CleanDatabase();
          });

  executor_.schedule_task(std::move(promise));
}

bool CrashpadAgent::UploadReport(const crashpad::UUID& local_report_id,
                                 const std::map<std::string, std::string>& annotations,
                                 const bool has_minidump) {
  if (settings_.upload_policy() == Settings::UploadPolicy::DISABLED) {
    FX_LOGS(INFO) << "upload to remote crash server disabled. Local crash report, ID "
                  << local_report_id.ToString() << ", available under "
                  << config_.crashpad_database.path;
    if (const auto status = database_->SkipReportUpload(
            local_report_id, crashpad::Metrics::CrashSkippedReason::kUploadsDisabled);
        status != crashpad::CrashReportDatabase::kNoError) {
      FX_LOGS(WARNING) << "error skipping local crash report upload (" << status << ")";
    }
    return true;
  } else if (settings_.upload_policy() == Settings::UploadPolicy::LIMBO) {
    // TODO(fxb/6049): put the limbo crash reports in the pending queue.
    return true;
  }

  // Read local crash report as an "upload" report.
  std::unique_ptr<const crashpad::CrashReportDatabase::UploadReport> report;
  if (const auto status = database_->GetReportForUploading(local_report_id, &report);
      status != crashpad::CrashReportDatabase::kNoError) {
    FX_LOGS(ERROR) << "error loading local crash report, ID " << local_report_id.ToString() << " ("
                   << status << ")";
    return false;
  }

  std::map<std::string, crashpad::FileReader*> attachments = report->GetAttachments();
  if (has_minidump) {
    attachments["uploadFileMinidump"] = report->Reader();
  }

  std::string server_report_id;
  if (!crash_server_->MakeRequest(annotations, attachments, &server_report_id)) {
    FX_LOGS(ERROR) << "error uploading local crash report, ID " << local_report_id.ToString();
    // Destruct the report to release the lockfile.
    report.reset();
    if (const auto status = database_->SkipReportUpload(
            local_report_id, crashpad::Metrics::CrashSkippedReason::kUploadFailed);
        status != crashpad::CrashReportDatabase::kNoError) {
      FX_LOGS(WARNING) << "error skipping local crash report upload (" << status << ")";
    }
    return false;
  }
  FX_LOGS(INFO) << "successfully uploaded crash report at https://crash.corp.google.com/"
                << server_report_id;
  if (const auto status = database_->RecordUploadComplete(std::move(report), server_report_id);
      status != crashpad::CrashReportDatabase::kNoError) {
    FX_LOGS(WARNING) << "error marking local crash report as uploaded (" << status << ")";
  }
  inspect_manager_->MarkReportAsUploaded(local_report_id.ToString(), server_report_id);

  return true;
}

size_t CrashpadAgent::PruneDatabase() {
  // We need to create a new condition every time we prune as it internally maintains a cumulated
  // total size as it iterates over the reports in the database and we want to reset that cumulated
  // total size every time we prune.
  crashpad::DatabaseSizePruneCondition pruning_condition(config_.crashpad_database.max_size_in_kb);
  const size_t num_pruned = crashpad::PruneCrashReportDatabase(database_.get(), &pruning_condition);
  if (num_pruned > 0) {
    FX_LOGS(INFO) << fxl::StringPrintf("Pruned %lu crash report(s)", num_pruned);
  }
  return num_pruned;
}

size_t CrashpadAgent::CleanDatabase() {
  // We set the |lockfile_ttl| to one day to ensure that reports in new aren't removed until
  // a period of time has passed in which it is certain they are orphaned.
  const size_t num_removed =
      static_cast<size_t>(database_->CleanDatabase(/*lockfile_ttl=*/60 * 60 * 24));
  if (num_removed > 0) {
    FX_LOGS(INFO) << fxl::StringPrintf("Removed %lu orphan file(s) from Crashpad database",
                                       num_removed);
  }
  return num_removed;
}

}  // namespace feedback
