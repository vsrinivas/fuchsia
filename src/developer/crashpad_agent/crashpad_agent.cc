// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/crashpad_agent/crashpad_agent.h"

#include <fuchsia/crash/cpp/fidl.h>
#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/mem/cpp/fidl.h>
#include <lib/syslog/cpp/logger.h>
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
    std::shared_ptr<::sys::ServiceDirectory> services) {
  Config config;

  if (files::IsFile(kOverrideConfigPath)) {
    const zx_status_t status = ParseConfig(kOverrideConfigPath, &config);
    if (status == ZX_OK) {
      return CrashpadAgent::TryCreate(std::move(services), std::move(config));
    }
    FX_LOGS(ERROR) << "failed to read override config file at "
                   << kOverrideConfigPath << ": " << status << " ("
                   << zx_status_get_string(status)
                   << "); falling back to default config file";
  }

  // We try to load the default config included in the package if no override
  // config was specified or we failed to parse it.
  const zx_status_t status = ParseConfig(kDefaultConfigPath, &config);
  if (status == ZX_OK) {
    return CrashpadAgent::TryCreate(std::move(services), std::move(config));
  }
  FX_LOGS(ERROR) << "failed to read default config file at "
                 << kDefaultConfigPath << ": " << status << " ("
                 << zx_status_get_string(status) << ")";

  FX_LOGS(FATAL) << "failed to set up crash analyzer";
  return nullptr;
}

std::unique_ptr<CrashpadAgent> CrashpadAgent::TryCreate(
    std::shared_ptr<::sys::ServiceDirectory> services, Config config) {
  std::unique_ptr<CrashServer> crash_server;
  if (config.enable_upload_to_crash_server && config.crash_server_url) {
    crash_server = std::make_unique<CrashServer>(*config.crash_server_url);
  }
  return CrashpadAgent::TryCreate(std::move(services), std::move(config),
                                  std::move(crash_server));
}

std::unique_ptr<CrashpadAgent> CrashpadAgent::TryCreate(
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
      new CrashpadAgent(std::move(services), std::move(config),
                        std::move(database), std::move(crash_server)));
}

CrashpadAgent::CrashpadAgent(
    std::shared_ptr<::sys::ServiceDirectory> services, Config config,
    std::unique_ptr<crashpad::CrashReportDatabase> database,
    std::unique_ptr<CrashServer> crash_server)
    : services_(services),
      config_(std::move(config)),
      database_(std::move(database)),
      crash_server_(std::move(crash_server)) {
  FXL_DCHECK(services_);
  FXL_DCHECK(database_);
  if (config.enable_upload_to_crash_server) {
    FXL_DCHECK(crash_server_);
  }
}

void CrashpadAgent::OnNativeException(zx::process process, zx::thread thread,
                                      zx::port exception_port,
                                      OnNativeExceptionCallback callback) {
  const zx_status_t status = OnNativeException(
      std::move(process), std::move(thread), std::move(exception_port));
  Analyzer_OnNativeException_Result result;
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to handle native exception. Won't retry.";
    result.set_err(status);
  } else {
    Analyzer_OnNativeException_Response response;
    result.set_response(response);
  }
  callback(std::move(result));
  PruneDatabase();
}

void CrashpadAgent::OnManagedRuntimeException(
    std::string component_url, ManagedRuntimeException exception,
    OnManagedRuntimeExceptionCallback callback) {
  const zx_status_t status =
      OnManagedRuntimeException(component_url, std::move(exception));
  Analyzer_OnManagedRuntimeException_Result result;
  if (status != ZX_OK) {
    FX_LOGS(ERROR)
        << "failed to handle managed runtime exception. Won't retry.";
    result.set_err(status);
  } else {
    Analyzer_OnManagedRuntimeException_Response response;
    result.set_response(response);
  }
  callback(std::move(result));
  PruneDatabase();
}

void CrashpadAgent::OnKernelPanicCrashLog(
    fuchsia::mem::Buffer crash_log, OnKernelPanicCrashLogCallback callback) {
  const zx_status_t status = OnKernelPanicCrashLog(std::move(crash_log));
  Analyzer_OnKernelPanicCrashLog_Result result;
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to process kernel panic crash log. Won't retry.";
    result.set_err(status);
  } else {
    Analyzer_OnKernelPanicCrashLog_Response response;
    result.set_response(response);
  }
  callback(std::move(result));
  PruneDatabase();
}

zx_status_t CrashpadAgent::GetFeedbackData(Data* data) {
  if (!feedback_data_provider_) {
    services_->Connect(feedback_data_provider_.NewRequest());
  }

  fuchsia::feedback::DataProvider_GetData_Result out_result;
  const zx_status_t status = feedback_data_provider_->GetData(&out_result);
  if (status != ZX_OK) {
    return status;
  }
  if (out_result.is_err()) {
    return out_result.err();
  }

  *data = std::move(out_result.response().data);
  return ZX_OK;
}

Data CrashpadAgent::GetFeedbackData() {
  Data data;
  const zx_status_t get_status = GetFeedbackData(&data);
  if (get_status != ZX_OK) {
    FX_LOGS(WARNING) << "error fetching feedback data: " << get_status << " ("
                     << zx_status_get_string(get_status) << ")";
  }
  return data;
}

namespace {

std::string GetPackageName(const zx::process& process) {
  char name[ZX_MAX_NAME_LEN];
  if (process.get_property(ZX_PROP_NAME, name, sizeof(name)) == ZX_OK) {
    return std::string(name);
  }
  return std::string("unknown-package");
}

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

zx_status_t CrashpadAgent::OnNativeException(zx::process process,
                                             zx::thread thread,
                                             zx::port exception_port) {
  const std::string package_name = GetPackageName(process);
  FX_LOGS(INFO) << "generating crash report for exception thrown by "
                << package_name;

  // Prepare annotations and attachments.
  Data feedback_data = GetFeedbackData();
  const std::map<std::string, std::string> annotations =
      MakeDefaultAnnotations(feedback_data, package_name);
  const std::map<std::string, fuchsia::mem::Buffer> attachments =
      MakeAttachments(&feedback_data);

  // Set minidump and create local crash report.
  //   * The annotations will be stored in the minidump of the report and
  //     augmented with modules' annotations.
  //   * The attachments will be stored in the report.
  // We don't pass an upload_thread so we can do the upload ourselves
  // synchronously.
  crashpad::CrashReportExceptionHandler exception_handler(
      database_.get(), /*upload_thread=*/nullptr, &annotations, &attachments,
      /*user_stream_data_sources=*/nullptr);
  crashpad::UUID local_report_id;
  if (!exception_handler.HandleExceptionHandles(
          process, thread, zx::unowned_port(exception_port),
          &local_report_id)) {
    database_->SkipReportUpload(
        local_report_id,
        crashpad::Metrics::CrashSkippedReason::kPrepareForUploadFailed);
    FX_LOGS(ERROR) << "error handling exception for local crash report, ID "
                   << local_report_id.ToString();
    return ZX_ERR_INTERNAL;
  }

  // For userspace, we read back the annotations from the minidump instead of
  // passing them as argument like for kernel crashes because the Crashpad
  // handler augmented them with the modules' annotations.
  return UploadReport(local_report_id, /*annotations=*/nullptr,
                      /*read_annotations_from_minidump=*/true);
}

zx_status_t CrashpadAgent::OnManagedRuntimeException(
    std::string component_url, ManagedRuntimeException exception) {
  FX_LOGS(INFO) << "generating crash report for exception thrown by "
                << component_url;

  crashpad::CrashReportDatabase::OperationStatus database_status;

  // Create local crash report.
  std::unique_ptr<crashpad::CrashReportDatabase::NewReport> report;
  database_status = database_->PrepareNewCrashReport(&report);
  if (database_status != crashpad::CrashReportDatabase::kNoError) {
    FX_LOGS(ERROR) << "error creating local crash report (" << database_status
                   << ")";
    return ZX_ERR_INTERNAL;
  }

  // Prepare annotations and attachments.
  const Data feedback_data = GetFeedbackData();
  const std::map<std::string, std::string> annotations =
      MakeManagedRuntimeExceptionAnnotations(feedback_data, component_url,
                                             &exception);
  AddManagedRuntimeExceptionAttachments(report.get(), feedback_data,
                                        &exception);

  // Finish new local crash report.
  crashpad::UUID local_report_id;
  database_status = database_->FinishedWritingCrashReport(std::move(report),
                                                          &local_report_id);
  if (database_status != crashpad::CrashReportDatabase::kNoError) {
    FX_LOGS(ERROR) << "error writing local crash report (" << database_status
                   << ")";
    return ZX_ERR_INTERNAL;
  }

  return UploadReport(local_report_id, &annotations,
                      /*read_annotations_from_minidump=*/false);
}

zx_status_t CrashpadAgent::OnKernelPanicCrashLog(
    fuchsia::mem::Buffer crash_log) {
  FX_LOGS(INFO) << "generating crash report for previous kernel panic";

  crashpad::CrashReportDatabase::OperationStatus database_status;

  // Create local crash report.
  std::unique_ptr<crashpad::CrashReportDatabase::NewReport> report;
  database_status = database_->PrepareNewCrashReport(&report);
  if (database_status != crashpad::CrashReportDatabase::kNoError) {
    FX_LOGS(ERROR) << "error creating local crash report (" << database_status
                   << ")";
    return ZX_ERR_INTERNAL;
  }

  // Prepare annotations and attachments.
  const Data feedback_data = GetFeedbackData();
  const std::map<std::string, std::string> annotations =
      MakeDefaultAnnotations(feedback_data, /*package_name=*/"kernel");
  AddKernelPanicAttachments(report.get(), feedback_data, std::move(crash_log));

  // Finish new local crash report.
  crashpad::UUID local_report_id;
  database_status = database_->FinishedWritingCrashReport(std::move(report),
                                                          &local_report_id);
  if (database_status != crashpad::CrashReportDatabase::kNoError) {
    FX_LOGS(ERROR) << "error writing local crash report (" << database_status
                   << ")";
    return ZX_ERR_INTERNAL;
  }

  return UploadReport(local_report_id, &annotations,
                      /*read_annotations_from_minidump=*/false);
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
