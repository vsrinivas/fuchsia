// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/queue.h"

#include <lib/async/cpp/task.h>
#include <zircon/errors.h>

#include "src/developer/feedback/crashpad_agent/info/queue_info.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

using async::PostDelayedTask;
using async::PostTask;
using crashpad::FileReader;
using crashpad::UUID;
using UploadPolicy = feedback::Settings::UploadPolicy;

std::unique_ptr<Queue> Queue::TryCreate(async_dispatcher_t* dispatcher,
                                        std::shared_ptr<InfoContext> info_context,
                                        CrashServer* crash_server) {
  auto database = Database::TryCreate(info_context);
  if (!database) {
    return nullptr;
  }

  return std::unique_ptr<Queue>(
      new Queue(dispatcher, std::move(info_context), std::move(database), crash_server));
}

void Queue::WatchSettings(feedback::Settings* settings) {
  settings->RegisterUploadPolicyWatcher(
      [this](const UploadPolicy& upload_policy) { OnUploadPolicyChange(upload_policy); });
}

Queue::Queue(async_dispatcher_t* dispatcher, std::shared_ptr<InfoContext> info_context,
             std::unique_ptr<Database> database, CrashServer* crash_server)
    : dispatcher_(dispatcher),
      database_(std::move(database)),
      crash_server_(crash_server),
      info_(std::move(info_context)) {
  FXL_DCHECK(dispatcher_);
  FXL_DCHECK(database_);

  ProcessAllEveryHour();
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

  info_.LogReport(program_name, local_report_id.ToString());

  pending_reports_.push_back(local_report_id);

  // We do the processing and garbage collection asynchronously as we don't want to block the
  // caller.
  if (const auto status = PostTask(dispatcher_,
                                   [this] {
                                     ProcessAll();
                                     database_->GarbageCollect();
                                   });

      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Error posting task to process reports after adding new report";
  }

  return true;
}

size_t Queue::ProcessAll() {
  switch (state_) {
    case State::Archive:
      return ArchiveAll();
    case State::Upload:
      return UploadAll();
    case State::LeaveAsPending:
      return 0;
  }
}

bool Queue::Upload(const UUID& local_report_id) {
  auto report = database_->GetUploadReport(local_report_id);
  if (!report) {
    // The database no longer contains the report (it was most likely pruned).
    // Return true so the report is not processed again.
    return true;
  }

  info_.IncrementUploadAttempt(local_report_id.ToString());

  std::string server_report_id;
  if (crash_server_->MakeRequest(report->GetAnnotations(), report->GetAttachments(),
                                 &server_report_id)) {
    FX_LOGS(INFO) << "Successfully uploaded crash report at https://crash.corp.google.com/"
                  << server_report_id;
    database_->MarkAsUploaded(std::move(report), server_report_id);
    return true;
  }

  FX_LOGS(ERROR) << "Error uploading local crash report " << local_report_id.ToString();

  return false;
}

size_t Queue::UploadAll() {
  std::vector<UUID> new_pending_reports;
  for (const auto& local_report_id : pending_reports_) {
    if (!Upload(local_report_id)) {
      new_pending_reports.push_back(local_report_id);
    }
  }

  pending_reports_.swap(new_pending_reports);

  // |new_pending_reports| now contains the pending reports before attempting to upload them.
  return new_pending_reports.size() - pending_reports_.size();
}

size_t Queue::ArchiveAll() {
  size_t successful = 0;
  for (const auto& local_report_id : pending_reports_) {
    if (database_->Archive(local_report_id)) {
      ++successful;
    }
  }

  pending_reports_.clear();
  return successful;
}

// The queue is inheritly conservative with uploading crash reports meaning that a report that is
// forbidden from being uploaded will never be uploaded while crash reports that are permitted to be
// uploaded may later be considered to be forbidden. This is due to the fact that when uploads are
// disabled all reports are immediately archived after having been added to the queue, thus we never
// have to worry that a report that shouldn't be uploaded ends up being uploaded when the upload
// policy changes.
void Queue::OnUploadPolicyChange(const Settings::UploadPolicy& upload_policy) {
  switch (upload_policy) {
    case UploadPolicy::DISABLED:
      state_ = State::Archive;
      break;
    case UploadPolicy::ENABLED:
      state_ = State::Upload;
      break;
    case UploadPolicy::LIMBO:
      state_ = State::LeaveAsPending;
      break;
  }
  ProcessAll();
}

void Queue::ProcessAllEveryHour() {
  if (const auto status = PostDelayedTask(
          dispatcher_,
          [this] {
            if (ProcessAll() != 0) {
              FX_LOGS(INFO) << "Hourly processing of pending crash reports queue";
            }
            ProcessAllEveryHour();
          },
          zx::hour(1));
      status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Error posting hourly process task to async loop. Won't retry.";
  }
}

}  // namespace feedback
