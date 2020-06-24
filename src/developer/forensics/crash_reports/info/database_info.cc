// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/info/database_info.h"

#include <lib/syslog/cpp/macros.h>

namespace forensics {
namespace crash_reports {

DatabaseInfo::DatabaseInfo(std::shared_ptr<InfoContext> context) : context_(context) {
  FX_CHECK(context_);
}

void DatabaseInfo::CrashpadError(const cobalt::CrashpadFunctionError function) {
  context_->Cobalt().LogOccurrence(function);
}

void DatabaseInfo::LogMaxCrashpadDatabaseSize(const uint64_t max_crashpad_database_size_in_kb) {
  context_->InspectManager().ExposeDatabase(max_crashpad_database_size_in_kb);
}

void DatabaseInfo::LogGarbageCollection(const uint64_t num_cleaned, const uint64_t num_pruned) {
  context_->InspectManager().IncreaseReportsCleanedBy(num_cleaned);
  context_->InspectManager().IncreaseReportsPrunedBy(num_pruned);
}

void DatabaseInfo::RecordUploadAttemptNumber(const std::string& local_report_id,
                                             const uint64_t upload_attempt) {
  context_->InspectManager().SetUploadAttempt(local_report_id, upload_attempt);
  context_->Cobalt().LogCount(cobalt::UploadAttemptState::kUploadAttempt, upload_attempt);
}

void DatabaseInfo::MarkReportAsUploaded(const std::string& local_report_id,
                                        const std::string& server_report_id,
                                        const uint64_t upload_attempts) {
  context_->InspectManager().MarkReportAsUploaded(local_report_id, server_report_id);
  context_->Cobalt().LogOccurrence(cobalt::CrashState::kUploaded);
  context_->Cobalt().LogCount(cobalt::UploadAttemptState::kUploaded, upload_attempts);
}

void DatabaseInfo::MarkReportAsArchived(const std::string& local_report_id,
                                        const uint64_t upload_attempts) {
  context_->InspectManager().MarkReportAsArchived(local_report_id);
  context_->Cobalt().LogOccurrence(cobalt::CrashState::kArchived);

  // We log if it was attempted at least once.
  if (upload_attempts > 0) {
    context_->Cobalt().LogCount(cobalt::UploadAttemptState::kArchived, upload_attempts);
  }
}

void DatabaseInfo::MarkReportAsGarbageCollected(const std::string& local_report_id,
                                                const uint64_t upload_attempts) {
  context_->InspectManager().MarkReportAsGarbageCollected(local_report_id);
  context_->Cobalt().LogOccurrence(cobalt::CrashState::kGarbageCollected);

  // We log if it was attempted at least once.
  if (upload_attempts > 0) {
    context_->Cobalt().LogCount(cobalt::UploadAttemptState::kGarbageCollected, upload_attempts);
  }
}

}  // namespace crash_reports
}  // namespace forensics
