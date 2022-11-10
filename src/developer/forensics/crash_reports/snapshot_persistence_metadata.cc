// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/snapshot_persistence_metadata.h"

#include <lib/syslog/cpp/macros.h>

#include <filesystem>
#include <utility>

#include "src/developer/forensics/crash_reports/snapshot.h"
#include "src/developer/forensics/utils/storage_size.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/path.h"

namespace forensics::crash_reports {

namespace fs = std::filesystem;

SnapshotPersistenceMetadata::SnapshotPersistenceMetadata(std::string snapshot_store_root,
                                                         StorageSize max_size)
    : snapshot_store_root_(std::move(snapshot_store_root)),
      max_size_(max_size),
      is_directory_usable_(false) {
  RecreateFromFilesystem();
}

bool SnapshotPersistenceMetadata::Contains(const SnapshotUuid& uuid) const {
  return snapshot_metadata_.find(uuid) != snapshot_metadata_.end();
}

bool SnapshotPersistenceMetadata::RecreateFromFilesystem() {
  current_size_ = StorageSize::Bytes(0);
  snapshot_metadata_.clear();

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

StorageSize SnapshotPersistenceMetadata::CurrentSize() const { return current_size_; }

StorageSize SnapshotPersistenceMetadata::RemainingSpace() const {
  return max_size_ - current_size_;
}

const std::string& SnapshotPersistenceMetadata::RootDir() const { return snapshot_store_root_; }

void SnapshotPersistenceMetadata::Add(const SnapshotUuid& uuid, StorageSize size,
                                      std::string_view archive_key) {
  FX_CHECK(IsDirectoryUsable());
  current_size_ += size;

  snapshot_metadata_[uuid].size = size;
  snapshot_metadata_[uuid].dir = files::JoinPath(snapshot_store_root_, uuid);
  snapshot_metadata_[uuid].snapshot_key = archive_key;
}

void SnapshotPersistenceMetadata::Delete(const SnapshotUuid& uuid) {
  FX_CHECK(IsDirectoryUsable());
  FX_CHECK(Contains(uuid)) << "Contains() should be called before any Delete()";

  current_size_ -= snapshot_metadata_[uuid].size;
  snapshot_metadata_.erase(uuid);
}

std::vector<SnapshotUuid> SnapshotPersistenceMetadata::SnapshotUuids() const {
  std::vector<SnapshotUuid> uuids;
  for (const auto& [uuid, _] : snapshot_metadata_) {
    uuids.push_back(uuid);
  }

  return uuids;
}

StorageSize SnapshotPersistenceMetadata::SnapshotSize(const SnapshotUuid& uuid) const {
  FX_CHECK(Contains(uuid));

  return snapshot_metadata_.at(uuid).size;
}

std::string SnapshotPersistenceMetadata::SnapshotDirectory(const SnapshotUuid& uuid) const {
  FX_CHECK(Contains(uuid));

  return snapshot_metadata_.at(uuid).dir;
}

std::string SnapshotPersistenceMetadata::SnapshotKey(const SnapshotUuid& uuid) const {
  FX_CHECK(Contains(uuid));

  return snapshot_metadata_.at(uuid).snapshot_key;
}

}  // namespace forensics::crash_reports
