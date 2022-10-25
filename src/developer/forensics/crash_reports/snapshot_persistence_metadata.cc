// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/snapshot_persistence_metadata.h"

#include <lib/syslog/cpp/macros.h>

#include <filesystem>

#include "src/developer/forensics/utils/storage_size.h"
#include "src/lib/files/directory.h"

namespace forensics::crash_reports {

namespace fs = std::filesystem;

SnapshotPersistenceMetadata::SnapshotPersistenceMetadata(std::string snapshot_store_root)
    : snapshot_store_root_(std::move(snapshot_store_root)), is_directory_usable_(false) {
  RecreateFromFilesystem();
}

bool SnapshotPersistenceMetadata::Contains(const SnapshotUuid& uuid) const {
  return snapshot_metadata_.find(uuid) != snapshot_metadata_.end();
}

bool SnapshotPersistenceMetadata::RecreateFromFilesystem() {
  current_size_ = StorageSize::Bytes(0);
  if (!files::IsDirectory(snapshot_store_root_) && !files::CreateDirectory(snapshot_store_root_)) {
    FX_LOGS(WARNING) << "Failed to create " << snapshot_store_root_;
    is_directory_usable_ = false;
    return false;
  }

  for (const auto& snapshot_dir : fs::directory_iterator(snapshot_store_root_)) {
    const auto& snapshot_path = snapshot_dir.path();
    const SnapshotUuid uuid = snapshot_path.filename();

    for (const auto& file : fs::directory_iterator(snapshot_dir)) {
      const auto& filename = file.path().filename();
      if (filename == ".") {
        continue;
      }

      if (snapshot_metadata_.find(uuid) != snapshot_metadata_.end()) {
        FX_LOGS(ERROR) << "Found more than 1 file stored in snapshot directory '" << snapshot_dir
                       << "'";
        continue;
      }

      std::error_code ec;
      const auto snapshot_size = StorageSize::Bytes(fs::file_size(file.path(), ec));

      if (ec) {
        FX_LOGS(ERROR) << "Failed to read filesize for snapshot uuid '" << uuid << "'";
      }

      current_size_ += snapshot_size;

      snapshot_metadata_[uuid].size = snapshot_size;
      snapshot_metadata_[uuid].dir = snapshot_path;
      snapshot_metadata_[uuid].snapshot_key = filename;
    }
  }

  is_directory_usable_ = true;
  return true;
}

bool SnapshotPersistenceMetadata::IsDirectoryUsable() const { return is_directory_usable_; }

std::string SnapshotPersistenceMetadata::SnapshotDirectory(const SnapshotUuid& uuid) const {
  FX_CHECK(Contains(uuid));

  return snapshot_metadata_.at(uuid).dir;
}

std::string SnapshotPersistenceMetadata::SnapshotKey(const SnapshotUuid& uuid) const {
  FX_CHECK(Contains(uuid));

  return snapshot_metadata_.at(uuid).snapshot_key;
}

}  // namespace forensics::crash_reports
