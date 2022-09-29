// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SNAPSHOT_COLLECTOR_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SNAPSHOT_COLLECTOR_H_

#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/fpromise/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "src/developer/forensics/crash_reports/product.h"
#include "src/developer/forensics/crash_reports/report.h"
#include "src/developer/forensics/crash_reports/reporting_policy_watcher.h"
#include "src/developer/forensics/crash_reports/snapshot.h"
#include "src/developer/forensics/crash_reports/snapshot_store.h"
#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/developer/forensics/feedback_data/data_provider.h"
#include "src/lib/timekeeper/clock.h"

namespace forensics {
namespace crash_reports {

// Manages the collection of snapshots.
//
// To limit memory usage, SnapshotCollector will return the same Uuid to all calls to GetUuid that
// occur within  |shared_request_window_| of a fuchsia.feedback.DataProvider/GetSnapshot request.
class SnapshotCollector {
 public:
  SnapshotCollector(async_dispatcher_t* dispatcher, timekeeper::Clock* clock,
                    feedback_data::DataProviderInternal* data_provider,
                    SnapshotStore* snapshot_store, zx::duration shared_request_window);

  // Returns a promise of a report. The report may have a snapshot uuid, with that snapshot
  // containing the most up-to-date system data (a new snapshot will be created if all existing
  // snapshots contain data that is out-of-date). No snapshot will be saved if |timeout| expires.
  ::fpromise::promise<Report> GetReport(zx::duration timeout,
                                        fuchsia::feedback::CrashReport fidl_report,
                                        ReportId report_id,
                                        std::optional<timekeeper::time_utc> current_utc_time,
                                        const Product& product, bool is_hourly_snapshot,
                                        ReportingPolicy reporting_policy);

  // Shuts down the snapshot manager by cancelling any pending FIDL calls and provides waiting
  // clients with a UUID for a generic "shutdown" snapshot.
  void Shutdown();

 private:
  // State associated with an async call to fuchsia.feedback.DataProvider/GetSnapshot. If a
  // SnapshotRequest exists, it is implicitly pending.
  struct SnapshotRequest {
    ~SnapshotRequest() {
      for (auto& blocked_promise : blocked_promises) {
        if (blocked_promise) {
          blocked_promise.resume_task();
        }
      }
    }

    // The uuid of the request's snapshot.
    SnapshotUuid uuid;

    // Ids of pending promises associated with this request. There should be one promise for each
    // report using this snapshot request.
    std::set<uint64_t> promise_ids;

    // Promises that are waiting on the call to complete.
    std::vector<::fpromise::suspended_task> blocked_promises;

    // The actual request that we delay by |shared_request_window_| after the SnapshotRequest is
    // created.
    async::TaskClosure delayed_get_snapshot;
  };

  struct ReportResults {
    // The uuid of the report's snapshot.
    SnapshotUuid uuid;

    // The annotations manually added plus annotations extracted from the report's snapshot.
    std::shared_ptr<feedback::Annotations> annotations;
  };

  // Determine if the most recent SnapshotRequest's delayed call to
  // fuchsia.feedback.DataProvider/GetSnapshopt has executed.
  bool UseLatestRequest() const;

  // Find the Snapshot{Request,Data} with Uuid |uuid|. If none exists, return nullptr.
  SnapshotRequest* FindSnapshotRequest(const SnapshotUuid& uuid);

  // Resume |get_uuid_promise| when |deadline| expires or request |uuid| completes with a snapshot.
  void WaitForSnapshot(const SnapshotUuid& uuid, zx::time deadline,
                       ::fpromise::suspended_task get_uuid_promise);

  // Make a call to fuchsia.feedback.DataProvider/GetSnapshot, started at |start_time|, and return
  // the Uuid of its eventual snapshot.
  SnapshotUuid MakeNewSnapshotRequest(zx::time start_time, zx::duration timeout);

  // Use |fidl_snapshot| to update all snapshot-related state with Uuid |uuid|.
  void CompleteWithSnapshot(const SnapshotUuid& uuid, feedback::Annotations annotations,
                            fuchsia::feedback::Attachment archive);

  // Retrieves the MissingSnapshot from the store and returns the combination of annotations and
  // presence annotations.
  feedback::Annotations GetMissingSnapshotAnnotations(const SnapshotUuid& uuid);

  async_dispatcher_t* dispatcher_;
  std::shared_ptr<sys::ServiceDirectory> services_;
  timekeeper::Clock* clock_;

  feedback_data::DataProviderInternal* data_provider_;
  SnapshotStore* snapshot_store_;

  zx::duration shared_request_window_;

  std::vector<std::unique_ptr<SnapshotRequest>> snapshot_requests_;
  std::map<uint64_t, std::optional<ReportResults>> report_results_;

  bool shutdown_{false};
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SNAPSHOT_COLLECTOR_H_
