// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SNAPSHOT_PERSISTENCE_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SNAPSHOT_PERSISTENCE_H_

#include <memory>
#include <string>

#include "src/developer/forensics/crash_reports/snapshot.h"
#include "src/developer/forensics/crash_reports/snapshot_persistence_metadata.h"

namespace forensics::crash_reports {

// Stores the contents of snapshots on disk. Does not perform any automatic garbage collection if
// the snapshot storage limits have been reached.
class SnapshotPersistence {
 public:
  // A directory to store snapshots under and the maximum amount of data that can be stored under
  // that directory before adds fail.
  struct Root {
    std::string dir;
    StorageSize max_size;
  };

  SnapshotPersistence(const Root& temp_root, const Root& persistent_root);

  // Adds a snapshot to persistence. Returns true if successful.
  bool Add(const SnapshotUuid& uuid, const ManagedSnapshot::Archive& archive,
           StorageSize archive_size);

  // Returns true if a snapshot for |uuid| exists on disk.
  bool Contains(const SnapshotUuid& uuid) const;

  // Gets an archive from disk. Check-fails that the archive for |uuid| exists on disk. Call
  // Contains to verify existence on disk first.
  std::shared_ptr<const ManagedSnapshot::Archive> Get(const SnapshotUuid& uuid);

  // Deletes the snapshot for |uuid| from persistence. Returns true if successful.
  bool Delete(const SnapshotUuid& uuid);

 private:
  // Adds a snapshot to persistence. Returns true if successful.
  bool AddToRoot(const SnapshotUuid& uuid, const ManagedSnapshot::Archive& archive,
                 StorageSize archive_size, SnapshotPersistenceMetadata& root);

  // The root that the snapshot for |uuid| is stored under.
  SnapshotPersistenceMetadata& RootFor(const SnapshotUuid& uuid);

  // Pick the root to store an archive with size of |archive_size| under. Returns nullptr if neither
  // root has enough space for the archive.
  SnapshotPersistenceMetadata* PickRootForStorage(StorageSize archive_size);

  // Returns a storage root that can be used if |root| fails.
  SnapshotPersistenceMetadata& FallbackRoot(const SnapshotPersistenceMetadata& root);

  // Returns true if another storage root can be used.
  bool HasFallbackRoot(const SnapshotPersistenceMetadata& root) const;

  SnapshotPersistenceMetadata tmp_metadata_;
  SnapshotPersistenceMetadata cache_metadata_;
};

}  // namespace forensics::crash_reports

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SNAPSHOT_PERSISTENCE_H_
