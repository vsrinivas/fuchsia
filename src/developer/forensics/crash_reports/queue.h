// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_QUEUE_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_QUEUE_H_

#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>

#include <map>
#include <unordered_map>
#include <vector>

#include "src/developer/forensics/crash_reports/crash_server.h"
#include "src/developer/forensics/crash_reports/info/info_context.h"
#include "src/developer/forensics/crash_reports/info/queue_info.h"
#include "src/developer/forensics/crash_reports/log_tags.h"
#include "src/developer/forensics/crash_reports/network_watcher.h"
#include "src/developer/forensics/crash_reports/report.h"
#include "src/developer/forensics/crash_reports/report_id.h"
#include "src/developer/forensics/crash_reports/reporting_policy_watcher.h"
#include "src/developer/forensics/crash_reports/store.h"
#include "src/lib/fxl/macros.h"

namespace forensics {
namespace crash_reports {

// Queues pending reports and processes them according to the reporting policy.
class Queue {
 public:
  Queue(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
        std::shared_ptr<InfoContext> info_context, LogTags* tags, CrashServer* crash_server,
        SnapshotManager* snapshot_manager);

  // Watcher functions that allow the queue to react to external events, such as
  //  1) the reporting policy changing or
  //  2) the network status changing.
  void WatchReportingPolicy(ReportingPolicyWatcher* watcher);
  void WatchNetwork(NetworkWatcher* network_watcher);

  // Add a report to the queue.
  bool Add(Report report);

  uint64_t Size() const { return pending_reports_.size(); }
  bool IsEmpty() const { return pending_reports_.empty(); }
  ReportId LatestReport() { return pending_reports_.back().report_id; }
  bool Contains(ReportId report_id) const;
  bool HasHourlyReport() const;
  bool IsPeriodicUploadScheduled() const {
    return upload_all_every_fifteen_minutes_task_.is_pending();
  }

  // Forces the queue to automatically put all reports in the store and stop all uploads.
  void StopUploading();

 private:
  // Information about a report that needs to be uploaded, including its id, its snapshot uuid, and
  // whether it's the sole hourly report.
  struct PendingReport {
    explicit PendingReport(const Report& report)
        : report_id(report.Id()),
          snapshot_uuid(report.SnapshotUuid()),
          is_hourly_report(report.IsHourlyReport()) {}

    PendingReport(const ReportId report_id, const SnapshotUuid snapshot_uuid,
                  const bool is_hourly_report)
        : report_id(report_id),
          snapshot_uuid(std::move(snapshot_uuid)),
          is_hourly_report(is_hourly_report) {}

    ReportId report_id;
    SnapshotUuid snapshot_uuid;
    bool is_hourly_report;
  };

  // Why a report is being retired.
  enum class RetireReason { kUpload, kDelete, kThrottled, kTimedOut, kArchive, kGarbageCollected };

  // Utility class for recording metrics about reports.
  class UploadMetrics {
   public:
    explicit UploadMetrics(std::shared_ptr<InfoContext> info_context)
        : info_(std::move(info_context)) {}

    void IncrementUploadAttempts(ReportId report_id);

    // Record |report_id| as being retired and erase any state associated with it.
    void Retire(ReportId report_id, RetireReason retire_reason, std::string server_report_id = "");

   private:
    QueueInfo info_;
    std::map<ReportId, size_t> upload_attempts_;
  };

  // Stop using the report associated with |pending_report| for the provided reason. Resources
  // associated with the report will be cleaned up.
  void Retire(PendingReport pending_report, RetireReason reason, std::string server_report_id = "");

  // Attempts to upload all pending reports and removes the successfully uploaded reports from the
  // queue. Returns the number of reports successfully uploaded.
  size_t UploadAll();

  // Deletes all of the reports in the queue and store.
  void DeleteAll();

  // Attempts to upload a report
  //
  // Returns false if another upload attempt should be made in the future.
  bool Upload(const Report& report);

  // Schedules UploadAll() to run every 15 minutes.
  void UploadAllEveryFifteenMinutes();

  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  LogTags* tags_;
  Store store_;
  CrashServer* crash_server_;
  SnapshotManager* snapshot_manager_;
  UploadMetrics metrics_;

  std::deque<PendingReport> pending_reports_;

  async::TaskClosureMethod<Queue, &Queue::UploadAllEveryFifteenMinutes>
      upload_all_every_fifteen_minutes_task_{this};

  ReportingPolicy reporting_policy_{ReportingPolicy::kUndecided};
  bool stop_uploading_{false};

  FXL_DISALLOW_COPY_AND_ASSIGN(Queue);
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_QUEUE_H_
