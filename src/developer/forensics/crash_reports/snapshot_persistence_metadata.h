// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SNAPSHOT_PERSISTENCE_METADATA_H_
#define SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SNAPSHOT_PERSISTENCE_METADATA_H_

#include <map>
#include <string>

#include "src/developer/forensics/crash_reports/snapshot.h"
#include "src/developer/forensics/utils/storage_size.h"

namespace forensics::crash_reports {

// In-memory metadata about the snapshot store on disk at |snapshot_store_root|.
class SnapshotPersistenceMetadata {
 public:
  explicit SnapshotPersistenceMetadata(std::string snapshot_store_root);

  // Recreates the metadata from the snapshot store at |snapshot_store_root_|.
  //
  // Returns false if the |metadata| does not accurately represent the filesystem and the underlying
  // directory can't safely be used.
  bool RecreateFromFilesystem();

  // Returns true if the directory underlying the SnapshotPersistenceMetadata can safely be used.
  bool IsDirectoryUsable() const;

  bool Contains(const SnapshotUuid& uuid) const;

  // Returns the directory that contains the snapshot |uuid|.
  std::string SnapshotDirectory(const SnapshotUuid& uuid) const;

  // Returns the key for the snapshot |uuid|.
  std::string SnapshotKey(const SnapshotUuid& uuid) const;

 private:
  // Metadata about each snapshot including:
  //  1) Its total size.
  //  2) The directory it is stored in.
  //  3) The key for the snapshot archive.
  struct SnapshotMetadata {
    StorageSize size;
    std::string dir;
    std::string snapshot_key;
  };

  // Where the snapshot store is located in the filesystem.
  std::string snapshot_store_root_;

  StorageSize current_size_;

  bool is_directory_usable_;

  std::map<SnapshotUuid, SnapshotMetadata> snapshot_metadata_;
};

}  // namespace forensics::crash_reports

#endif  // SRC_DEVELOPER_FORENSICS_CRASH_REPORTS_SNAPSHOT_PERSISTENCE_METADATA_H_
