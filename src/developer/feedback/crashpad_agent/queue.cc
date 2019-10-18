// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/queue.h"

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

using crashpad::FileReader;
using crashpad::UUID;

std::unique_ptr<Queue> Queue::TryCreate(CrashpadDatabaseConfig database_config,
                                        CrashServer* crash_server,
                                        InspectManager* inspect_manager) {
  auto database = Database::TryCreate(database_config);
  if (!database) {
    return nullptr;
  }

  return std::unique_ptr<Queue>(new Queue(std::move(database), crash_server, inspect_manager));
}

Queue::Queue(std::unique_ptr<Database> database, CrashServer* crash_server,
             InspectManager* inspect_manager)
    : database_(std::move(database)),
      crash_server_(crash_server),
      inspect_manager_(inspect_manager) {
  FXL_DCHECK(database_);
  FXL_DCHECK(crash_server_);
  FXL_DCHECK(inspect_manager_);
}

bool Queue::Contains(const UUID& uuid) const {
  return std::find(pending_reports_.begin(), pending_reports_.end(), uuid) !=
         pending_reports_.end();
}

bool Queue::Add(const std::string& program_name,
                std::map<std::string, fuchsia::mem::Buffer> attachments,
                std::optional<fuchsia::mem::Buffer> minidump,
                std::map<std::string, std::string> annotations) {
  UUID local_report_id;
  if (!database_->MakeNewReport(attachments, minidump, annotations, &local_report_id)) {
    return false;
  }

  inspect_manager_->AddReport(program_name, local_report_id.ToString());

  pending_reports_.push_back(local_report_id);
  ProcessAll();
  database_->GarbageCollect();
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

bool Queue::Upload(const UUID& local_report_id) {
  auto report = database_->GetUploadReport(local_report_id);
  if (!report) {
    // The database no longer contains the report (it was most likely pruned).
    // Return true so the report is not processed again.
    return true;
  }

  std::string server_report_id;
  if (crash_server_->MakeRequest(report->GetAnnotations(), report->GetAttachments(),
                                 &server_report_id)) {
    FX_LOGS(INFO) << "Successfully uploaded crash report at https://crash.corp.google.com/"
                  << server_report_id;
    database_->MarkAsUploaded(std::move(report), server_report_id);
    inspect_manager_->MarkReportAsUploaded(local_report_id.ToString(), server_report_id);
    return true;
  }

  FX_LOGS(ERROR) << "Error uploading local crash report " << local_report_id.ToString();

  return false;
}

void Queue::UploadAll() {
  std::vector<UUID> new_pending_reports;
  for (const auto& local_report_id : pending_reports_) {
    if (!Upload(local_report_id)) {
      new_pending_reports.push_back(local_report_id);
    }
  }
  pending_reports_.swap(new_pending_reports);
}

void Queue::ArchiveAll() {
  for (const auto& local_report_id : pending_reports_) {
    database_->Archive(local_report_id);
  }

  pending_reports_.clear();
}
}  // namespace feedback
