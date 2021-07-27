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

  bool Add(Report report);

  uint64_t Size() const;
  bool IsEmpty() const;
  ReportId LatestReport() const;
  bool Contains(ReportId report_id) const;
  bool IsPeriodicUploadScheduled() const;

  // Returns true if there is an hourly report already an hourly report anywhere in the queue, i.e.
  // active, ready or blocked.
  bool HasHourlyReport() const;

  // Forces the queue to automatically put all reports in the store and stop all uploads.
  void StopUploading();

 private:
  // Internal representation of a report including metadata about the report and an optional
  // in-memory version of the report.
  //
  // Note: |report| will be set iff it is actively being uploaded or hasn't been added to the store.
  struct PendingReport {
    explicit PendingReport(Report report);
    PendingReport(ReportId report_id, SnapshotUuid snapshot_uuid, bool is_hourly_report);

    PendingReport(const PendingReport&) = delete;
    PendingReport& operator=(const PendingReport&) = delete;
    PendingReport(PendingReport&&) = default;
    PendingReport& operator=(PendingReport&&) = default;

    // Utility method for interacting with |report|.
    void SetReport(Report report);
    Report TakeReport();
    bool HasReport() const;

    ReportId report_id;
    SnapshotUuid snapshot_uuid;
    bool is_hourly_report;
    std::optional<Report> report;

    // Set to true iff the report is the active report and needs to be deleted once it becomes
    // blocked.
    bool delete_post_upload;
  };

  // Why a report is being retired.
  enum class RetireReason { kUpload, kDelete, kThrottled, kTimedOut, kArchive, kGarbageCollected };

  // Instantiates the queue from state in the store.
  void InitFromStore();

  // Internal Add() method with information on whether the report can be uploaded immediately and
  // put in the store. |consider_eager_upload| and |add_to_store| will be false if the report
  // already has an upload attempt or put in the store, respectively.
  bool Add(PendingReport pending_report, bool consider_eager_upload, bool add_to_store);

  bool AddToStore(Report report);

  // Stops using |pending_report| for the provided reason and cleans up its resources.
  void Retire(PendingReport pending_report, RetireReason reason, std::string server_report_id = "");

  // Attempts to upload all reports in |ready_reports_|. Reports are retired if they're uploaded or
  // throttled and re-added to the queue if the upload fails.
  void Upload();

  // Make all reports blocked.
  void BlockAll();

  // Makes all reports ready and call Upload.
  void UnblockAll();

  void DeleteAll();
  void UnblockAllEveryFifteenMinutes();

  std::string ReportIdsStr(const std::deque<PendingReport>& reports) const;

  // Utility class for recording metrics about reports.
  class UploadMetrics {
   public:
    explicit UploadMetrics(std::shared_ptr<InfoContext> info_context);
    void IncrementUploadAttempts(ReportId report_id);

    // Record |report_id| as being retired and erase any state associated with it.
    void Retire(const PendingReport& pending_report, RetireReason retire_reason,
                std::string server_report_id = "");

   private:
    QueueInfo info_;
    std::map<ReportId, size_t> upload_attempts_;
  };

  async_dispatcher_t* dispatcher_;
  const std::shared_ptr<sys::ServiceDirectory> services_;
  LogTags* tags_;
  Store store_;
  CrashServer* crash_server_;
  SnapshotManager* snapshot_manager_;
  UploadMetrics metrics_;

  async::TaskClosureMethod<Queue, &Queue::UnblockAllEveryFifteenMinutes>
      unblock_all_every_fifteen_minutes_task_{this};

  ReportingPolicy reporting_policy_{ReportingPolicy::kUndecided};
  bool stop_uploading_{false};

  // A report is either:
  //  1) Active (actively being uploaded).
  //  2) Ready (can become the active report).
  //  3) Blocked (not ready or active and won't become so unless a stimulus triggers it, e.g., the
  //  network becoming reachable).
  std::optional<PendingReport> active_report_;
  std::deque<PendingReport> ready_reports_;
  std::deque<PendingReport> blocked_reports_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Queue);
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_QUEUE_H_
