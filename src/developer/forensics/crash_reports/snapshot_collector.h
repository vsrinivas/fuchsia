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

  // Returns a promise of a snapshot uuid for a snapshot that contains the most up-to-date system
  // data (a new snapshot will be created if all existing snapshots contain data that is
  // out-of-date). No uuid will be returned if |timeout| expires.
  ::fpromise::promise<SnapshotUuid> GetSnapshotUuid(zx::duration timeout);

  // Removes request for |uuid|. Check-fails that there are no blocked promises for the request.
  // TODO(fxbug.dev/108830): Add internal accounting for number of parties interested in a request
  // and make this a private function.
  void RemoveRequest(const SnapshotUuid& uuid);

  // Shuts down the snapshot manager by cancelling any pending FIDL calls and provides waiting
  // clients with a UUID for a generic "shutdown" snapshot.
  void Shutdown();

 private:
  // State associated with an async call to fuchsia.feedback.DataProvider/GetSnapshot.
  struct SnapshotRequest {
    // The uuid of the request's snapshot.
    SnapshotUuid uuid;

    // Whether the request is pending.
    bool is_pending;

    // Promises that are waiting on the call to complete.
    std::vector<::fpromise::suspended_task> blocked_promises;

    // The actual request that we delay by |shared_request_window_| after the SnapshotRequest is
    // created.
    async::TaskClosure delayed_get_snapshot;
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

  // Remove the annotations and archives of the oldest requests, independently of one another, until
  // |current_annotations_size_| is less than or equal to |max_annotations_size_| and
  // |current_archives_size_| is less than or equal to |max_archives_size_|.
  //
  // Note: References into |requests_| and |data_| will be invalidated during this process. Be
  // cautious using the function!
  void EnforceSizeLimits();

  async_dispatcher_t* dispatcher_;
  std::shared_ptr<sys::ServiceDirectory> services_;
  timekeeper::Clock* clock_;

  feedback_data::DataProviderInternal* data_provider_;
  SnapshotStore* snapshot_store_;

  zx::duration shared_request_window_;

  std::vector<std::unique_ptr<SnapshotRequest>> requests_;

  bool shutdown_{false};
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SNAPSHOT_COLLECTOR_H_
