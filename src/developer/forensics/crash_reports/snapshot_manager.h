// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SNAPSHOT_MANAGER_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SNAPSHOT_MANAGER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/promise.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/time.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "src/developer/forensics/crash_reports/snapshot.h"
#include "src/developer/forensics/utils/storage_size.h"
#include "src/lib/timekeeper/clock.h"

namespace forensics {
namespace crash_reports {

using SnapshotUuid = std::string;

// Manages the collection, distribution, and lifetime of snapshots.
//
// To limit memory usage, the managed snapshots' annotations/archives cannot exceed
// |max_{annotations,archives}_size_| in size and snapshot manager will return the same Uuid to all
// calls to GetUuid that occur within  |shared_request_window_| of a
// fuchsia.feedback.DataProvider/GetSnapshot request.
//
// When space is constrained, SnapshotManager will drop the oldest annotations/archives it
// manages. Additionally, SnapshotManager tracks the number of clients that have received a specific
// Uuid from GetUuid and will automatically delete a snapshot when each client has called Release.
class SnapshotManager {
 public:
  SnapshotManager(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
                  std::unique_ptr<timekeeper::Clock> clock, zx::duration shared_request_window,
                  StorageSize max_annotations_size, StorageSize max_archives_size);

  // Returns a promise of a snapshot uuid. No uuid will be returned if |timeout| expires.
  ::fit::promise<SnapshotUuid> GetSnapshotUuid(zx::duration timeout);

  // Returns the snapshot for |uuid|, if one exists. If no snapshot exists for |uuid| a snapshot
  // containing annotations indicating the error will be returned.
  //
  // When a client no longer needs the data contained in a Snapshot, they should call Release to
  // inform the SnapshotManager. If all clients call release, the SnapshotManager will voluntarily
  // drop the Snapshot, freeing up space for new data.
  Snapshot GetSnapshot(const SnapshotUuid& uuid);

  // Tell SnapshotManager that a client no longer needs the snapshot for |uuid|. If the difference
  // between the number of calls to GetUuid and Release reaches 0, the snapshot for |uuid| will be
  // dropped by SnapshotManager.
  void Release(const SnapshotUuid& uuid);

  // Returns a Uuid a client can use if it doesn't have one, e.g., it was previously stored in a
  // file and the file is gone.
  static SnapshotUuid UuidForNoSnapshotUuid() { return "no uuid"; }

 private:
  // State associated with an async call to fuchsia.feedback.DataProvider/GetSnapshot.
  //  * The uuid of the request's snapshot.
  //  * The time the request was made.
  //  * Whether the request is pending.
  //  * Promises that are waiting on the call to complete.
  struct SnapshotRequest {
    SnapshotUuid uuid;
    zx::time start_time;
    bool is_pending;
    std::vector<::fit::suspended_task> blocked_promises;
  };

  // State associated with a snapshot.
  //   * The number of clients with its uuid.
  //   * The size of its annotations.
  //   * The size of its archive.
  //   * The snapshot annotations.
  //   * The snapshot archive.
  struct SnapshotData {
    size_t num_clients_with_uuid;
    StorageSize annotations_size;
    StorageSize archive_size;
    std::shared_ptr<Snapshot::Annotations> annotations;
    std::shared_ptr<Snapshot::Archive> archive;
  };

  // Connect to fuchsia.feedback.DataProvider.
  void Connect();

  // Determine if |time| is in [|start_time|, |start_time| + |shared_request_window_|) of the most
  // recent request.
  bool UseLatestRequest(zx::time time) const;

  // Find the Snapshot{Request,Data} with Uuid |uuid|. If none exists, return nullptr.
  SnapshotRequest* FindSnapshotRequest(const SnapshotUuid& uuid);
  SnapshotData* FindSnapshotData(const SnapshotUuid& uuid);

  // Resume |get_uuid_promise| when |deadline| expires or request |uuid| completes with a snapshot.
  void WaitForSnapshot(const SnapshotUuid& uuid, zx::time deadline,
                       ::fit::suspended_task get_uuid_promise);

  // Make a call to fuchsia.feedback.DataProvider/GetSnapshot, started at |start_time|, and return
  // the Uuid of its eventual snapshot.
  SnapshotUuid MakeNewSnapshotRequest(zx::time start_time, zx::duration timeout);

  // Use |fidl_snapshot| to update all snapshot-related state with Uuid |uuid|.
  void CompleteWithSnapshot(const SnapshotUuid& uuid, fuchsia::feedback::Snapshot fidl_snapshot);

  // Remove the annotations and archives of the oldest requests, independently of one another, until
  // |current_annotations_size_| is less than or equal to |max_annotations_size_| and
  // |current_archives_size_| is less than or equal to |max_archives_size_|.
  //
  // Note: References into |requests_| and |data_| will be invalidated during this process. Be
  // cautious using the function!
  void EnforceSizeLimits();

  // Drop the {annotation,archive} for |uuid| and clean up state associated with them.
  void DropAnnotations(SnapshotData* data);
  void DropArchive(SnapshotData* data);

  async_dispatcher_t* dispatcher_;
  std::shared_ptr<sys::ServiceDirectory> services_;
  std::unique_ptr<timekeeper::Clock> clock_;

  fuchsia::feedback::DataProviderPtr data_provider_;

  zx::duration shared_request_window_;

  StorageSize max_annotations_size_;
  StorageSize current_annotations_size_;

  StorageSize max_archives_size_;
  StorageSize current_archives_size_;

  std::vector<SnapshotRequest> requests_;
  std::map<SnapshotUuid, SnapshotData> data_;

  // SnapshotUuid and annotations to return under specific conditions, e.g., garbage collection,
  // time outs.
  struct SpecialCaseSnapshot {
    explicit SpecialCaseSnapshot(SnapshotUuid uuid)
        : uuid(std::move(uuid)), annotations(std::make_unique<Snapshot::Annotations>()) {}
    SnapshotUuid uuid;
    std::shared_ptr<Snapshot::Annotations> annotations;
  };

  SpecialCaseSnapshot garbage_collected_snapshot_;
  SpecialCaseSnapshot timed_out_snapshot_;
  SpecialCaseSnapshot no_uuid_snapshot_;
};

}  // namespace crash_reports
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SNAPSHOT_MANAGER_H_
