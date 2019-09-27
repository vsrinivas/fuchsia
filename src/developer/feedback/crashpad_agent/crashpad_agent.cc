// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/crashpad_agent.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/fit/result.h>
#include <lib/syslog/cpp/logger.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <map>
#include <string>
#include <utility>

#include "src/developer/feedback/crashpad_agent/config.h"
#include "src/developer/feedback/crashpad_agent/crash_server.h"
#include "src/developer/feedback/crashpad_agent/feedback_data_provider_ptr.h"
#include "src/developer/feedback/crashpad_agent/report_annotations.h"
#include "src/developer/feedback/crashpad_agent/report_attachments.h"
#include "src/developer/feedback/crashpad_agent/scoped_unlink.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "third_party/crashpad/client/crash_report_database.h"
#include "third_party/crashpad/client/prune_crash_reports.h"
#include "third_party/crashpad/util/misc/metrics.h"
#include "third_party/crashpad/util/misc/uuid.h"
#include "third_party/crashpad/util/net/http_body.h"
#include "third_party/crashpad/util/net/http_headers.h"
#include "third_party/crashpad/util/net/http_multipart_builder.h"
#include "third_party/crashpad/util/net/http_transport.h"
#include "third_party/crashpad/util/net/url.h"

namespace feedback {
namespace {

using crashpad::CrashReportDatabase;
using fuchsia::feedback::CrashReport;
using fuchsia::feedback::Data;

const char kDefaultConfigPath[] = "/pkg/data/default_config.json";
const char kOverrideConfigPath[] = "/config/data/override_config.json";

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

void CrashpadAgent::OnManagedRuntimeException(std::string component_url,
                                              fuchsia::crash::ManagedRuntimeException exception,
                                              OnManagedRuntimeExceptionCallback callback) {
  // This is a legacy flow using the old protocol. We convert its input arguments to the ones of the
  // new protocol so the rest of this class only needs to know about the new protocol.
  fuchsia::feedback::RuntimeCrashReport dart_report;
  dart_report.set_exception_type(
      std::string(reinterpret_cast<const char*>(exception.dart().type.data())));
  dart_report.set_exception_message(
      std::string(reinterpret_cast<const char*>(exception.dart().message.data())));
  dart_report.set_exception_stack_trace(std::move(exception.dart().stack_trace));
  fuchsia::feedback::SpecificCrashReport specific_report;
  specific_report.set_dart(std::move(dart_report));
  CrashReport report;
  report.set_program_name(std::move(component_url));
  report.set_specific_report(std::move(specific_report));
  File(std::move(report), [callback = std::move(callback)](fit::result<void, zx_status_t> result) {
    callback(std::move(result));
  });
}

void CrashpadAgent::File(fuchsia::feedback::CrashReport report, FileCallback callback) {
  if (!report.has_program_name()) {
    FX_LOGS(ERROR) << "Invalid crash report. No program name. Won't file.";
    callback(fit::error(ZX_ERR_INVALID_ARGS));
    return;
  }

  auto promise =
      File(std::move(report))
          .or_else([] {
            FX_LOGS(ERROR) << "Failed to file crash report. Won't retry.";
            return fit::error(ZX_ERR_INTERNAL);
          })
          .then([callback = std::move(callback), this](fit::result<void, zx_status_t>& result) {
            callback(std::move(result));
            PruneDatabase();
            CleanDatabase();
          });

  executor_.schedule_task(std::move(promise));
}

fit::promise<void> CrashpadAgent::File(fuchsia::feedback::CrashReport report) {
  FX_LOGS(INFO) << "generating crash report for " << report.program_name();

  // Create local Crashpad report.
  std::unique_ptr<crashpad::CrashReportDatabase::NewReport> crashpad_report;
  if (const auto status = database_->PrepareNewCrashReport(&crashpad_report);
      status != crashpad::CrashReportDatabase::kNoError) {
    FX_LOGS(ERROR) << "error creating local Crashpad report (" << status << ")";
    return fit::make_error_promise();
  }

  return GetFeedbackData(dispatcher_, services_, /*timeout=*/zx::sec(10))
      .then([this, report = std::move(report), crashpad_report = std::move(crashpad_report)](
                fit::result<Data>& result) mutable -> fit::result<void> {
        Data feedback_data;
        if (result.is_ok()) {
          feedback_data = result.take_value();
        }

        const std::map<std::string, std::string> annotations =
            BuildAnnotations(report, feedback_data);
        BuildAttachments(report, feedback_data, crashpad_report.get());

        // Finish new local crash report.
        crashpad::UUID local_report_id;
        if (const auto status =
                database_->FinishedWritingCrashReport(std::move(crashpad_report), &local_report_id);
            status != crashpad::CrashReportDatabase::kNoError) {
          FX_LOGS(ERROR) << "error writing local Crashpad report (" << status << ")";
          return fit::error();
        }

        inspect_manager_->AddReport(report.program_name(), local_report_id.ToString());

        if (!UploadReport(local_report_id, annotations)) {
          return fit::error();
        }
        return fit::ok();
      });
}

bool CrashpadAgent::UploadReport(const crashpad::UUID& local_report_id,
                                 const std::map<std::string, std::string>& annotations) {
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

  // We have to build the MIME multipart message ourselves as all the public Crashpad helpers are
  // asynchronous and we won't be able to know the upload status nor the server report ID.
  crashpad::HTTPMultipartBuilder http_multipart_builder;
  http_multipart_builder.SetGzipEnabled(true);
  for (const auto& kv : annotations) {
    http_multipart_builder.SetFormData(kv.first, kv.second);
  }
  for (const auto& kv : report->GetAttachments()) {
    http_multipart_builder.SetFileAttachment(kv.first, kv.first, kv.second,
                                             "application/octet-stream");
  }
  http_multipart_builder.SetFileAttachment("uploadFileMinidump", report->uuid.ToString() + ".dmp",
                                           report->Reader(), "application/octet-stream");
  crashpad::HTTPHeaders content_headers;
  http_multipart_builder.PopulateContentHeaders(&content_headers);

  std::string server_report_id;
  if (!crash_server_->MakeRequest(content_headers, http_multipart_builder.GetBodyStream(),
                                  &server_report_id)) {
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

void CrashpadAgent::PruneDatabase() {
  // We need to create a new condition every time we prune as it internally maintains a cumulated
  // total size as it iterates over the reports in the database and we want to reset that cumulated
  // total size every time we prune.
  crashpad::DatabaseSizePruneCondition pruning_condition(config_.crashpad_database.max_size_in_kb);
  crashpad::PruneCrashReportDatabase(database_.get(), &pruning_condition);
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
