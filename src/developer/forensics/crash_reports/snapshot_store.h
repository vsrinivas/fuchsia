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
// When space is constrained, calling AddSnapshot will cause SnapshotStore to drop the oldest
// archive until there is enough space for the new archive.
class SnapshotStore {
 public:
  SnapshotStore(feedback::AnnotationManager* annotation_manager,
                std::string garbage_collected_snapshots_path, StorageSize max_archives_size);

  // Stores the given data in memory for later retrieval.
  void AddSnapshot(const SnapshotUuid& uuid, fuchsia::feedback::Attachment archive);

  // Deletes the data for |uuid| from memory, if it still exists.
  void DeleteSnapshot(const SnapshotUuid& uuid);

  // Returns true if data for |uuid| is currently stored in the SnapshotStore.
  bool SnapshotExists(const SnapshotUuid& uuid);

  // Returns the number of snapshots stored in SnapshotStore.
  //
  // Note: intended primarily for testing.
  //
  // TODO(fxbug.dev/111793): Consider deleting this function after reports with timed out snapshots
  // aren't associated with their original snapshot uuids (fxbug.dev/111317).
  size_t Size() const;

  // Returns true if data for |uuid| was garbage collected.
  bool IsGarbageCollected(const SnapshotUuid& uuid) const;

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

 private:
  // State associated with a snapshot.
  //   * The size of its archive.
  //   * The snapshot archive.
  struct SnapshotData {
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

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SNAPSHOT_STORE_H_
