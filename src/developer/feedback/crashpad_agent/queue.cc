// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/queue.h"

#include "src/lib/syslog/cpp/logger.h"

namespace feedback {
namespace {

using crashpad::CrashReportDatabase;
using crashpad::FileReader;
using crashpad::Metrics;
using crashpad::UUID;

}  // namespace

bool Queue::Contains(const UUID& uuid) const {
  return pending_report_request_parameters_.find(uuid.ToString()) !=
         pending_report_request_parameters_.end();
}

bool Queue::Add(UUID local_report_id, std::map<std::string, std::string> annotations,
                std::map<std::string, FileReader*> attachments) {
  if (Contains(local_report_id)) {
    FX_LOGS(ERROR) << "report " << local_report_id.ToString() << " already in the queue";
    return false;
  }
  pending_report_request_parameters_[local_report_id.ToString()] =
      RequestParameters{annotations, attachments};
  pending_reports_.push_back(local_report_id);
  ProcessAll();
  return true;
}

void Queue::ProcessAll() {
  switch (state_) {
    case State::Archive:
      ArchiveAll();
      break;
    case State::Upload:
      UploadAll();
      break;
    case State::LeaveAsPending:
      break;
  }
}

namespace {

// Mark a report as completed in the database and in Inspect.
bool MarkAsUploaded(const UUID& local_report_id, const std::string& server_report_id,
                    std::unique_ptr<const CrashReportDatabase::UploadReport> report,
                    CrashReportDatabase* database, InspectManager* inspect_manager) {
  if (const auto status = database->RecordUploadComplete(std::move(report), server_report_id);
      status != CrashReportDatabase::OperationStatus::kNoError) {
    FX_LOGS(ERROR) << "unable to record " << local_report_id.ToString()
                   << " as uploaded in database (" << status << ")";
    return false;
  }

  if (!inspect_manager->MarkReportAsUploaded(local_report_id.ToString(), server_report_id)) {
    return false;
  }
  return true;
}

// Attempt to upload a report to the crash server.
bool AttemptUpload(const UUID& local_report_id,
                   const std::map<std::string, std::string>& annotations,
                   const std::map<std::string, FileReader*>& attachments, CrashServer* crash_server,
                   std::string* server_report_id) {
  if (!crash_server->MakeRequest(annotations, attachments, server_report_id)) {
    FX_LOGS(INFO) << "error uploading local crash report, ID " << local_report_id.ToString();
    return false;
  }
  FX_LOGS(INFO) << "successfully uploaded crash report at https://crash.corp.google.com/"
                << server_report_id;
  return true;
}

// Get a report from the database. If it doesn't exist, return nullptr.
std::unique_ptr<const CrashReportDatabase::UploadReport> GetReportWithId(
    const UUID& local_report_id, CrashReportDatabase* database) {
  auto report = std::make_unique<const CrashReportDatabase::UploadReport>();
  if (database->GetReportForUploading(local_report_id, &report) != CrashReportDatabase::kNoError) {
    report.reset();  // Release |report|'s lockfile.
  }
  return report;
}
}  // namespace

bool Queue::Upload(const UUID& local_report_id) {
  auto report = GetReportWithId(local_report_id, database_);
  if (!report) {
    // The database no longer contains the report (it was most likely pruned).
    // Return true so the report is not processed again.
    return true;
  }

  const auto& annotations =
      pending_report_request_parameters_.at(local_report_id.ToString()).annotations;
  const auto& attachments =
      pending_report_request_parameters_.at(local_report_id.ToString()).attachments;
  std::string server_report_id;
  if (AttemptUpload(local_report_id, annotations, attachments, crash_server_, &server_report_id)) {
    MarkAsUploaded(local_report_id, server_report_id, std::move(report), database_,
                   inspect_manager_);
    return true;
  } else if (static_cast<uint64_t>(report->upload_attempts) == config_.max_upload_attempts - 1) {
    // |report->upload_attempts| is not incremented until the destruction of |report| thus there is
    // a discrepancy between the real number of upload attempts and |report|'s account of how many
    // times it has been attempted to be uploaded.
    report.reset();  // Release |report|'s lockfile.
    Archive(local_report_id);
    return true;
  }
  return false;
}

void Queue::UploadAll() {
  std::vector<UUID> new_pending_reports;
  for (const auto& local_report_id : pending_reports_) {
    if (Upload(local_report_id)) {
      pending_report_request_parameters_.erase(local_report_id.ToString());
    } else {
      new_pending_reports.push_back(local_report_id);
    }
  }
  pending_reports_.swap(new_pending_reports);
}

bool Queue::Archive(const UUID& local_report_id) {
  if (const auto status =
          database_->SkipReportUpload(local_report_id, Metrics::CrashSkippedReason::kUploadFailed);
      status != CrashReportDatabase::OperationStatus::kNoError) {
    FX_LOGS(ERROR) << "unable to record " << local_report_id.ToString()
                   << " as skipped in database_ (" << status << ")";
    return false;
  }
  return true;
}

void Queue::ArchiveAll() {
  for (const auto& local_report_id : pending_reports_) {
    Archive(local_report_id);
  }

  pending_reports_.clear();
  pending_report_request_parameters_.clear();
}
}  // namespace feedback
