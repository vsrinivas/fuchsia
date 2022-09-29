// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SNAPSHOT_STORE_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SNAPSHOT_STORE_H_

#include <deque>

#include "src/developer/forensics/crash_reports/snapshot.h"
#include "src/developer/forensics/feedback/annotations/annotation_manager.h"
#include "src/developer/forensics/utils/storage_size.h"

namespace forensics::crash_reports {

// Manages the distribution and lifetime of snapshots.
//
// To limit memory usage, the managed snapshots' archive cannot exceed |max_archives_size_| in size.
//
// When space is constrained, calling EnforceSizeLimits will tell SnapshotStore to drop the archives
// for |uuid|. Additionally, SnapshotStore tracks the number of clients that have called
// IncrementClientCount for a given Uuid and will automatically delete a snapshot when each client
// has called Release.
class SnapshotStore {
 public:
  SnapshotStore(feedback::AnnotationManager* annotation_manager,
                std::string garbage_collected_snapshots_path, StorageSize max_archives_size);

  // Starts a snapshot associated with |uuid| that doesn't have any data. Must be called before most
  // other functions requiring a uuid.
  void StartSnapshot(const SnapshotUuid& uuid);

  // Stores the given data in memory for later retrieval. Must call StartSnapshot for |uuid| first.
  void AddSnapshot(const SnapshotUuid& uuid, fuchsia::feedback::Attachment archive);

  // Tell SnapshotStore that an additional client needs the snapshot for |uuid|. Must call
  // StartSnapshot for |uuid| first.
  void IncrementClientCount(const SnapshotUuid& uuid);

  // Returns true if data for |uuid| is currently stored in the SnapshotStore.
  bool SnapshotExists(const SnapshotUuid& uuid);

  // Returns true if the size of the currently stored archives is greater than the limit.
  bool SizeLimitsExceeded() const;

  // Returns the snapshot for |uuid|, if one exists. If no snapshot exists for |uuid| a
  // MissingSnapshot containing annotations indicating the error will be returned.
  //
  // When a client no longer needs the data contained in a Snapshot, they should call Release to
  // inform the SnapshotStore. If all clients call release, the SnapshotStore will voluntarily
  // drop the Snapshot, freeing up space for new data.
  Snapshot GetSnapshot(const SnapshotUuid& uuid);

  // Returns the snapshot for |uuid|. Check-fails that |uuid| results in the return of a
  // MissingSnapshot. A MissingSnapshot is guaranteed to be generated if |uuid| is the uuid of a
  // SpecialCaseSnapshot.
  MissingSnapshot GetMissingSnapshot(const SnapshotUuid& uuid);

  // Tell SnapshotStore that a client no longer needs the snapshot for |uuid|. If the difference
  // between the number of calls to AddClient and Release reaches 0, the snapshot for |uuid| will be
  // dropped by SnapshotStore and the function will return true.
  bool Release(const SnapshotUuid& uuid);

 private:
  // State associated with a snapshot.
  //   * The number of clients with its uuid.
  //   * The size of its archive.
  //   * The snapshot archive.
  struct SnapshotData {
    size_t num_clients_with_uuid;
    StorageSize archive_size;
    std::shared_ptr<const ManagedSnapshot::Archive> archive;
  };

  // Drops archives if size limits are exceeded. Must call StartSnapshot for |uuid| first.
  void EnforceSizeLimits(const SnapshotUuid& uuid);

  // Drop the archive for |data| and clean up state associated with it.
  void DropArchive(SnapshotData* data);

  SnapshotData* FindSnapshotData(const SnapshotUuid& uuid);
  void RecordAsGarbageCollected(const SnapshotUuid& uuid);

  feedback::AnnotationManager* annotation_manager_;

  std::string garbage_collected_snapshots_path_;

  StorageSize max_archives_size_;
  StorageSize current_archives_size_;

  std::map<SnapshotUuid, SnapshotData> data_;
  std::deque<SnapshotUuid> insertion_order_;
  std::set<SnapshotUuid> garbage_collected_snapshots_;

  // SnapshotUuid and annotations to return under specific conditions, e.g., garbage collection,
  // time outs.
  struct SpecialCaseSnapshot {
    explicit SpecialCaseSnapshot(SnapshotUuid uuid, feedback::Annotations annotations)
        : uuid(std::move(uuid)), annotations(std::move(annotations)) {}
    SnapshotUuid uuid;
    feedback::Annotations annotations;
  };

  const SpecialCaseSnapshot garbage_collected_snapshot_;
  const SpecialCaseSnapshot not_persisted_snapshot_;
  const SpecialCaseSnapshot timed_out_snapshot_;
  const SpecialCaseSnapshot shutdown_snapshot_;
  const SpecialCaseSnapshot no_uuid_snapshot_;
};

}  // namespace forensics::crash_reports

#endif
