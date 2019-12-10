// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/info/database_info.h"

#include "src/developer/feedback/crashpad_agent/metrics_registry.cb.h"
#include "src/lib/fxl/logging.h"

namespace feedback {

using cobalt_registry::kCrashMetricId;

using CrashState = cobalt_registry::CrashMetricDimensionState;

DatabaseInfo::DatabaseInfo(std::shared_ptr<InfoContext> context) : context_(context) {
  FXL_CHECK(context);
}

void DatabaseInfo::LogMaxCrashpadDatabaseSizei(uint64_t max_crashpad_database_size_in_kb) {
  context_->InspectManager().ExposeDatabase(max_crashpad_database_size_in_kb);
}
void DatabaseInfo::MarkReportAsUploaded(const std::string& local_report_id,
                                        const std::string& server_report_id) {
  context_->InspectManager().MarkReportAsUploaded(local_report_id, server_report_id);
  context_->Cobalt().Log(kCrashMetricId, CrashState::Uploaded);
}

void DatabaseInfo::MarkReportAsArchived(const std::string& local_report_id) {
  context_->InspectManager().MarkReportAsArchived(local_report_id);
  context_->Cobalt().Log(kCrashMetricId, CrashState::Archived);
}

void DatabaseInfo::MarkReportAsGarbageCollected(const std::string& local_report_id) {
  context_->InspectManager().MarkReportAsGarbageCollected(local_report_id);
  context_->Cobalt().Log(kCrashMetricId, CrashState::GarbageCollected);
}

}  // namespace feedback
