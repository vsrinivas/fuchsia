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
  SnapshotPersistence(std::string temp_dir, std::string persistent_dir);

  // Returns true if a snapshot for |uuid| exists on disk.
  bool Contains(const SnapshotUuid& uuid) const;

  // Gets an archive from disk. Check-fails that the archive for |uuid| exists on disk. Call
  // Contains to verify existence on disk first.
  std::shared_ptr<const ManagedSnapshot::Archive> Get(const SnapshotUuid& uuid);

 private:
  // The root that the snapshot for |uuid| is stored under.
  SnapshotPersistenceMetadata& RootFor(const SnapshotUuid& uuid);

  SnapshotPersistenceMetadata tmp_metadata_;
  SnapshotPersistenceMetadata cache_metadata_;
};

}  // namespace forensics::crash_reports

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SNAPSHOT_PERSISTENCE_H_
