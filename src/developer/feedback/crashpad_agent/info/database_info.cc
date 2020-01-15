// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/info/database_info.h"

#include "src/developer/feedback/utils/cobalt_metrics.h"
#include "src/lib/fxl/logging.h"

namespace feedback {

DatabaseInfo::DatabaseInfo(std::shared_ptr<InfoContext> context) : context_(context) {
  FXL_CHECK(context);
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
  context_->Cobalt().LogCount(UploadAttemptState::kUploadAttempt, upload_attempt);
}

void DatabaseInfo::MarkReportAsUploaded(const std::string& local_report_id,
                                        const std::string& server_report_id,
                                        const uint64_t upload_attempts) {
  context_->InspectManager().MarkReportAsUploaded(local_report_id, server_report_id);
  context_->Cobalt().LogOccurrence(CrashState::kUploaded);
  context_->Cobalt().LogCount(UploadAttemptState::kUploaded, upload_attempts);
}

void DatabaseInfo::MarkReportAsArchived(const std::string& local_report_id,
                                        const uint64_t upload_attempts) {
  context_->InspectManager().MarkReportAsArchived(local_report_id);
  context_->Cobalt().LogOccurrence(CrashState::kArchived);

  // We log if it was attempted at least once.
  if (upload_attempts > 0) {
    context_->Cobalt().LogCount(UploadAttemptState::kArchived, upload_attempts);
  }
}

void DatabaseInfo::MarkReportAsGarbageCollected(const std::string& local_report_id,
                                                const uint64_t upload_attempts) {
  context_->InspectManager().MarkReportAsGarbageCollected(local_report_id);
  context_->Cobalt().LogOccurrence(CrashState::kGarbageCollected);

  // We log if it was attempted at least once.
  if (upload_attempts > 0) {
    context_->Cobalt().LogCount(UploadAttemptState::kGarbageCollected, upload_attempts);
  }
}

}  // namespace feedback
