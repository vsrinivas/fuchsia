// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/crashpad_agent/crashpad_agent.h"

#include <fuchsia/crash/cpp/fidl.h>
#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/fsl/handles/object_info.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>

#include <map>
#include <string>
#include <utility>

#include "src/developer/crashpad_agent/config.h"
#include "src/developer/crashpad_agent/crash_server.h"
#include "src/developer/crashpad_agent/report_annotations.h"
#include "src/developer/crashpad_agent/report_attachments.h"
#include "src/developer/crashpad_agent/scoped_unlink.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/unique_fd.h"
#include "third_party/crashpad/client/crash_report_database.h"
#include "third_party/crashpad/client/prune_crash_reports.h"
#include "third_party/crashpad/client/settings.h"
#include "third_party/crashpad/handler/fuchsia/crash_report_exception_handler.h"
#include "third_party/crashpad/handler/minidump_to_upload_parameters.h"
#include "third_party/crashpad/snapshot/minidump/process_snapshot_minidump.h"
#include "third_party/crashpad/third_party/mini_chromium/mini_chromium/base/files/file_path.h"
#include "third_party/crashpad/third_party/mini_chromium/mini_chromium/base/files/scoped_file.h"
#include "third_party/crashpad/util/file/file_io.h"
#include "third_party/crashpad/util/file/file_reader.h"
#include "third_party/crashpad/util/misc/metrics.h"
#include "third_party/crashpad/util/misc/uuid.h"
#include "third_party/crashpad/util/net/http_body.h"
#include "third_party/crashpad/util/net/http_headers.h"
#include "third_party/crashpad/util/net/http_multipart_builder.h"
#include "third_party/crashpad/util/net/http_transport.h"
#include "third_party/crashpad/util/net/url.h"

namespace fuchsia {
namespace crash {
namespace {

using fuchsia::feedback::Data;

const char kDefaultConfigPath[] = "/pkg/data/default_config.json";
const char kOverrideConfigPath[] = "/config/data/override_config.json";

}  // namespace

std::unique_ptr<CrashpadAgent> CrashpadAgent::TryCreate(
    async_dispatcher_t* dispatcher,
    std::shared_ptr<::sys::ServiceDirectory> services) {
  Config config;

  if (files::IsFile(kOverrideConfigPath)) {
    const zx_status_t status = ParseConfig(kOverrideConfigPath, &config);
    if (status == ZX_OK) {
      return CrashpadAgent::TryCreate(dispatcher, std::move(services),
                                      std::move(config));
    }
    FX_PLOGS(ERROR, status)
        << "Failed to read override config file at " << kOverrideConfigPath
        << " - falling back to default config file";
  }

  // We try to load the default config included in the package if no override
  // config was specified or we failed to parse it.
  const zx_status_t status = ParseConfig(kDefaultConfigPath, &config);
  if (status == ZX_OK) {
    return CrashpadAgent::TryCreate(dispatcher, std::move(services),
                                    std::move(config));
  }
  FX_PLOGS(ERROR, status) << "Failed to read default config file at "
                          << kDefaultConfigPath;

  FX_LOGS(FATAL) << "Failed to set up crash analyzer";
  return nullptr;
}

std::unique_ptr<CrashpadAgent> CrashpadAgent::TryCreate(
    async_dispatcher_t* dispatcher,
    std::shared_ptr<::sys::ServiceDirectory> services, Config config) {
  std::unique_ptr<CrashServer> crash_server;
  if (config.enable_upload_to_crash_server && config.crash_server_url) {
    crash_server = std::make_unique<CrashServer>(*config.crash_server_url);
  }
  return CrashpadAgent::TryCreate(dispatcher, std::move(services),
                                  std::move(config), std::move(crash_server));
}

std::unique_ptr<CrashpadAgent> CrashpadAgent::TryCreate(
    async_dispatcher_t* dispatcher,
    std::shared_ptr<::sys::ServiceDirectory> services, Config config,
    std::unique_ptr<CrashServer> crash_server) {
  if (!files::IsDirectory(config.local_crashpad_database_path)) {
    files::CreateDirectory(config.local_crashpad_database_path);
  }

  std::unique_ptr<crashpad::CrashReportDatabase> database(
      crashpad::CrashReportDatabase::Initialize(
          base::FilePath(config.local_crashpad_database_path)));
  if (!database) {
    FX_LOGS(ERROR) << "error initializing local crash report database at "
                   << config.local_crashpad_database_path;
    FX_LOGS(FATAL) << "failed to set up crash analyzer";
    return nullptr;
  }

  // Today we enable uploads here. In the future, this will most likely be set
  // in some external settings.
  database->GetSettings()->SetUploadsEnabled(
      config.enable_upload_to_crash_server);

  return std::unique_ptr<CrashpadAgent>(
      new CrashpadAgent(dispatcher, std::move(services), std::move(config),
                        std::move(database), std::move(crash_server)));
}

CrashpadAgent::CrashpadAgent(
    async_dispatcher_t* dispatcher,
    std::shared_ptr<::sys::ServiceDirectory> services, Config config,
    std::unique_ptr<crashpad::CrashReportDatabase> database,
    std::unique_ptr<CrashServer> crash_server)
    : dispatcher_(dispatcher),
      executor_(dispatcher),
      services_(services),
      config_(std::move(config)),
      database_(std::move(database)),
      crash_server_(std::move(crash_server)) {
  FXL_DCHECK(dispatcher_);
  FXL_DCHECK(services_);
  FXL_DCHECK(database_);
  if (config.enable_upload_to_crash_server) {
    FXL_DCHECK(crash_server_);
  }
}

void CrashpadAgent::OnNativeException(zx::process process, zx::thread thread,
                                      zx::port exception_port,
                                      OnNativeExceptionCallback callback) {
  auto promise =
      OnNativeException(std::move(process), std::move(thread),
                        std::move(exception_port))
          .and_then([] {
            Analyzer_OnNativeException_Result result;
            Analyzer_OnNativeException_Response response;
            result.set_response(response);
            return fit::ok(std::move(result));
          })
          .or_else([] {
            FX_LOGS(ERROR) << "Failed to handle native exception. Won't retry.";
            Analyzer_OnNativeException_Result result;
            result.set_err(ZX_ERR_INTERNAL);
            return fit::ok(std::move(result));
          })
          .and_then([callback = std::move(callback),
                     this](Analyzer_OnNativeException_Result& result) {
            callback(std::move(result));
            PruneDatabase();
          });

  executor_.schedule_task(std::move(promise));
}

void CrashpadAgent::OnManagedRuntimeException(
    std::string component_url, ManagedRuntimeException exception,
    OnManagedRuntimeExceptionCallback callback) {
  auto promise =
      OnManagedRuntimeException(component_url, std::move(exception))
          .and_then([] {
            Analyzer_OnManagedRuntimeException_Result result;
            Analyzer_OnManagedRuntimeException_Response response;
            result.set_response(response);
            return fit::ok(std::move(result));
          })
          .or_else([] {
            FX_LOGS(ERROR)
                << "Failed to handle managed runtime exception. Won't retry.";
            Analyzer_OnManagedRuntimeException_Result result;
            result.set_err(ZX_ERR_INTERNAL);
            return fit::ok(std::move(result));
          })
          .and_then([callback = std::move(callback),
                     this](Analyzer_OnManagedRuntimeException_Result& result) {
            callback(std::move(result));
            PruneDatabase();
          });

  executor_.schedule_task(std::move(promise));
}

void CrashpadAgent::OnKernelPanicCrashLog(
    fuchsia::mem::Buffer crash_log, OnKernelPanicCrashLogCallback callback) {
  auto promise =
      OnKernelPanicCrashLog(std::move(crash_log))
          .and_then([] {
            Analyzer_OnKernelPanicCrashLog_Result result;
            Analyzer_OnKernelPanicCrashLog_Response response;
            result.set_response(response);
            return fit::ok(std::move(result));
          })
          .or_else(
              [] {
                FX_LOGS(ERROR)
                    << "Failed to process kernel panic crash log. Won't retry.";
                Analyzer_OnKernelPanicCrashLog_Result result;
                result.set_err(ZX_ERR_INTERNAL);
                return fit::ok(std::move(result));
              })
          .and_then([callback = std::move(callback),
                     this](Analyzer_OnKernelPanicCrashLog_Result& result) {
            callback(std::move(result));
            PruneDatabase();
          });

  executor_.schedule_task(std::move(promise));
}

fit::promise<Data> CrashpadAgent::GetFeedbackData() {
  const uint64_t id = next_feedback_data_provider_id_++;
  feedback_data_providers_[id] =
      std::make_unique<FeedbackDataProvider>(dispatcher_, services_);
  return feedback_data_providers_[id]
      ->GetData(
          zx::msec(config_.feedback_data_collection_timeout_in_milliseconds))
      .then([this, id](fit::result<Data>& result) {
        // We close the connection to the feedback data provider and then
        // forward the result.
        if (feedback_data_providers_.erase(id) == 0) {
          FX_LOGS(ERROR)
              << "No fuchsia.feedback.DataProvider connection to close with id "
              << id;
        }
        return std::move(result);
      });
}

namespace {

std::map<std::string, fuchsia::mem::Buffer> MakeAttachments(
    Data* feedback_data) {
  std::map<std::string, fuchsia::mem::Buffer> attachments;
  if (feedback_data->has_attachments()) {
    for (auto& attachment : *feedback_data->mutable_attachments()) {
      attachments[attachment.key] = std::move(attachment.value);
    }
  }
  return attachments;
}

}  // namespace

fit::promise<void> CrashpadAgent::OnNativeException(zx::process process,
                                                    zx::thread thread,
                                                    zx::port exception_port) {
  const std::string process_name = fsl::GetObjectName(process.get());
  FX_LOGS(INFO) << "generating crash report for exception thrown by "
                << process_name;

  // Prepare annotations and attachments.
  return GetFeedbackData().then(
      [this, process = std::move(process), thread = std::move(thread),
       exception_port = std::move(exception_port),
       process_name](fit::result<Data>& result) mutable -> fit::result<void> {
        Data feedback_data;
        if (result.is_ok()) {
          feedback_data = result.take_value();
        }
        const std::map<std::string, std::string> annotations =
            MakeDefaultAnnotations(feedback_data, process_name);
        const std::map<std::string, fuchsia::mem::Buffer> attachments =
            MakeAttachments(&feedback_data);

        // Set minidump and create local crash report.
        //   * The annotations will be stored in the minidump of the report
        //     and augmented with modules' annotations.
        //   * The attachments will be stored in the report.
        // We don't pass an upload_thread so we can do the upload ourselves
        // synchronously.
        crashpad::CrashReportExceptionHandler exception_handler(
            database_.get(), /*upload_thread=*/nullptr, &annotations,
            &attachments,
            /*user_stream_data_sources=*/nullptr);
        crashpad::UUID local_report_id;
        if (!exception_handler.HandleExceptionHandles(
                process, thread, zx::unowned_port(exception_port),
                &local_report_id)) {
          database_->SkipReportUpload(
              local_report_id,
              crashpad::Metrics::CrashSkippedReason::kPrepareForUploadFailed);
          FX_LOGS(ERROR)
              << "error handling exception for local crash report, ID "
              << local_report_id.ToString();
          return fit::error();
        }

        // For userspace, we read back the annotations from the minidump
        // instead of passing them as argument like for kernel crashes because
        // the Crashpad handler augmented them with the modules' annotations.
        if (UploadReport(local_report_id, /*annotations=*/nullptr,
                         /*read_annotations_from_minidump=*/true) != ZX_OK) {
          return fit::error();
        }
        return fit::ok();
      });
}

fit::promise<void> CrashpadAgent::OnManagedRuntimeException(
    std::string component_url, ManagedRuntimeException exception) {
  FX_LOGS(INFO) << "generating crash report for exception thrown by "
                << component_url;

  // Create local crash report.
  std::unique_ptr<crashpad::CrashReportDatabase::NewReport> report;
  const crashpad::CrashReportDatabase::OperationStatus database_status =
      database_->PrepareNewCrashReport(&report);
  if (database_status != crashpad::CrashReportDatabase::kNoError) {
    FX_LOGS(ERROR) << "error creating local crash report (" << database_status
                   << ")";
    return fit::make_error_promise();
  }

  // Prepare annotations and attachments.
  return GetFeedbackData().then(
      [this, component_url, exception = std::move(exception),
       report = std::move(report)](
          fit::result<Data>& result) mutable -> fit::result<void> {
        Data feedback_data;
        if (result.is_ok()) {
          feedback_data = result.take_value();
        }
        const std::map<std::string, std::string> annotations =
            MakeManagedRuntimeExceptionAnnotations(feedback_data, component_url,
                                                   &exception);
        AddManagedRuntimeExceptionAttachments(report.get(), feedback_data,
                                              &exception);

        // Finish new local crash report.
        crashpad::UUID local_report_id;
        const crashpad::CrashReportDatabase::OperationStatus database_status =
            database_->FinishedWritingCrashReport(std::move(report),
                                                  &local_report_id);
        if (database_status != crashpad::CrashReportDatabase::kNoError) {
          FX_LOGS(ERROR) << "error writing local crash report ("
                         << database_status << ")";
          return fit::error();
        }

        if (UploadReport(local_report_id, &annotations,
                         /*read_annotations_from_minidump=*/false) != ZX_OK) {
          return fit::error();
        }
        return fit::ok();
      });
}

fit::promise<void> CrashpadAgent::OnKernelPanicCrashLog(
    fuchsia::mem::Buffer crash_log) {
  FX_LOGS(INFO) << "generating crash report for previous kernel panic";

  // Create local crash report.
  std::unique_ptr<crashpad::CrashReportDatabase::NewReport> report;
  const crashpad::CrashReportDatabase::OperationStatus database_status =
      database_->PrepareNewCrashReport(&report);
  if (database_status != crashpad::CrashReportDatabase::kNoError) {
    FX_LOGS(ERROR) << "error creating local crash report (" << database_status
                   << ")";
    return fit::make_error_promise();
  }

  // Prepare annotations and attachments.
  return GetFeedbackData().then(
      [this, crash_log = std::move(crash_log), report = std::move(report)](
          fit::result<Data>& result) mutable -> fit::result<void> {
        Data feedback_data;
        if (result.is_ok()) {
          feedback_data = result.take_value();
        }
        const std::map<std::string, std::string> annotations =
            MakeDefaultAnnotations(feedback_data,
                                   /*package_name=*/"kernel");
        AddKernelPanicAttachments(report.get(), feedback_data,
                                  std::move(crash_log));

        // Finish new local crash report.
        crashpad::UUID local_report_id;
        const crashpad::CrashReportDatabase::OperationStatus database_status =
            database_->FinishedWritingCrashReport(std::move(report),
                                                  &local_report_id);
        if (database_status != crashpad::CrashReportDatabase::kNoError) {
          FX_LOGS(ERROR) << "error writing local crash report ("
                         << database_status << ")";
          return fit::error();
        }

        if (UploadReport(local_report_id, &annotations,
                         /*read_annotations_from_minidump=*/false) != ZX_OK) {
          return fit::error();
        }
        return fit::ok();
      });
}

zx_status_t CrashpadAgent::UploadReport(
    const crashpad::UUID& local_report_id,
    const std::map<std::string, std::string>* annotations,
    bool read_annotations_from_minidump) {
  bool uploads_enabled;
  if ((!database_->GetSettings()->GetUploadsEnabled(&uploads_enabled) ||
       !uploads_enabled)) {
    FX_LOGS(INFO)
        << "upload to remote crash server disabled. Local crash report, ID "
        << local_report_id.ToString() << ", available under "
        << config_.local_crashpad_database_path;
    database_->SkipReportUpload(
        local_report_id,
        crashpad::Metrics::CrashSkippedReason::kUploadsDisabled);
    return ZX_OK;
  }

  // Read local crash report as an "upload" report.
  std::unique_ptr<const crashpad::CrashReportDatabase::UploadReport> report;
  const crashpad::CrashReportDatabase::OperationStatus database_status =
      database_->GetReportForUploading(local_report_id, &report);
  if (database_status != crashpad::CrashReportDatabase::kNoError) {
    FX_LOGS(ERROR) << "error loading local crash report, ID "
                   << local_report_id.ToString() << " (" << database_status
                   << ")";
    return ZX_ERR_INTERNAL;
  }

  // Set annotations, either from argument or from minidump.
  FXL_CHECK((annotations != nullptr) ^ read_annotations_from_minidump);
  const std::map<std::string, std::string>* final_annotations = annotations;
  std::map<std::string, std::string> minidump_annotations;
  if (read_annotations_from_minidump) {
    crashpad::FileReader* reader = report->Reader();
    crashpad::FileOffset start_offset = reader->SeekGet();
    crashpad::ProcessSnapshotMinidump minidump_process_snapshot;
    if (!minidump_process_snapshot.Initialize(reader)) {
      report.reset();
      database_->SkipReportUpload(
          local_report_id,
          crashpad::Metrics::CrashSkippedReason::kPrepareForUploadFailed);
      FX_LOGS(ERROR) << "error processing minidump for local crash report, ID "
                     << local_report_id.ToString();
      return ZX_ERR_INTERNAL;
    }
    minidump_annotations = crashpad::BreakpadHTTPFormParametersFromMinidump(
        &minidump_process_snapshot);
    final_annotations = &minidump_annotations;
    if (!reader->SeekSet(start_offset)) {
      report.reset();
      database_->SkipReportUpload(
          local_report_id,
          crashpad::Metrics::CrashSkippedReason::kPrepareForUploadFailed);
      FX_LOGS(ERROR) << "error processing minidump for local crash report, ID "
                     << local_report_id.ToString();
      return ZX_ERR_INTERNAL;
    }
  }

  // We have to build the MIME multipart message ourselves as all the public
  // Crashpad helpers are asynchronous and we won't be able to know the upload
  // status nor the server report ID.
  crashpad::HTTPMultipartBuilder http_multipart_builder;
  http_multipart_builder.SetGzipEnabled(true);
  for (const auto& kv : *final_annotations) {
    http_multipart_builder.SetFormData(kv.first, kv.second);
  }
  for (const auto& kv : report->GetAttachments()) {
    http_multipart_builder.SetFileAttachment(kv.first, kv.first, kv.second,
                                             "application/octet-stream");
  }
  http_multipart_builder.SetFileAttachment(
      "uploadFileMinidump", report->uuid.ToString() + ".dmp", report->Reader(),
      "application/octet-stream");
  crashpad::HTTPHeaders content_headers;
  http_multipart_builder.PopulateContentHeaders(&content_headers);

  std::string server_report_id;
  if (!crash_server_->MakeRequest(content_headers,
                                  http_multipart_builder.GetBodyStream(),
                                  &server_report_id)) {
    report.reset();
    database_->SkipReportUpload(
        local_report_id, crashpad::Metrics::CrashSkippedReason::kUploadFailed);
    FX_LOGS(ERROR) << "error uploading local crash report, ID "
                   << local_report_id.ToString();
    return ZX_ERR_INTERNAL;
  }
  database_->RecordUploadComplete(std::move(report), server_report_id);
  FX_LOGS(INFO) << "successfully uploaded crash report at "
                   "https://crash.corp.google.com/"
                << server_report_id;

  return ZX_OK;
}

void CrashpadAgent::PruneDatabase() {
  // We need to create a new condition every time we prune as it internally
  // maintains a cumulated total size as it iterates over the reports in the
  // database and we want to reset that cumulated total size every time we
  // prune.
  crashpad::DatabaseSizePruneCondition pruning_condition(
      config_.max_crashpad_database_size_in_kb);
  crashpad::PruneCrashReportDatabase(database_.get(), &pruning_condition);
}

}  // namespace crash
}  // namespace fuchsia
