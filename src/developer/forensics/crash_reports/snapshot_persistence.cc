// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/snapshot_persistence.h"

#include <lib/syslog/cpp/macros.h>

#include <optional>
#include <utility>
#include <vector>

#include "src/developer/forensics/crash_reports/snapshot.h"
#include "src/developer/forensics/crash_reports/snapshot_persistence_metadata.h"
#include "src/developer/forensics/utils/sized_data.h"
#include "src/developer/forensics/utils/storage_size.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"

namespace forensics::crash_reports {

namespace {

bool ReadSnapshot(const std::string& path, SizedData* snapshot) {
  return files::ReadFileToVector(path, snapshot);
}

bool DeletePath(const std::string& path) { return files::DeletePath(path, /*recursive=*/true); }

bool WriteData(const std::string& path, const SizedData& attachment) {
  return files::WriteFile(path, reinterpret_cast<const char*>(attachment.data()),
                          attachment.size());
}

bool SpaceAvailable(const SnapshotPersistenceMetadata& root, StorageSize archive_size) {
  return root.RemainingSpace() >= archive_size;
}

// Get the contents of a directory without ".".
std::vector<std::string> GetDirectoryContents(const std::string& dir) {
  std::vector<std::string> contents;
  files::ReadDirContents(dir, &contents);

  contents.erase(std::remove(contents.begin(), contents.end(), "."), contents.end());
  return contents;
}

// Recursively delete empty directories under |root|, including |root| if it is empty or becomes
// empty.
void RemoveEmptyDirectories(const std::string& root) {
  const std::vector<std::string> contents = GetDirectoryContents(root);
  if (contents.empty()) {
    DeletePath(root);
    return;
  }

  for (const auto& content : contents) {
    const std::string path = files::JoinPath(root, content);
    if (files::IsDirectory(path)) {
      RemoveEmptyDirectories(path);
    }
  }

  if (GetDirectoryContents(root).empty()) {
    DeletePath(root);
  }
}

}  // namespace

SnapshotPersistence::SnapshotPersistence(const std::optional<Root>& temp_root,
                                         const std::optional<Root>& persistent_root)

{
  if (temp_root.has_value()) {
    tmp_metadata_ = SnapshotPersistenceMetadata(temp_root->dir, temp_root->max_size);

    // Clean up any empty directories in tmp. This may happen if the component stops running while
    // it is deleting a snapshot.
    RemoveEmptyDirectories(tmp_metadata_->RootDir());

    // |temp_root.dir| must be usable immediately.
    FX_CHECK(tmp_metadata_->RecreateFromFilesystem());
  }

  if (persistent_root.has_value()) {
    cache_metadata_ = SnapshotPersistenceMetadata(persistent_root->dir, persistent_root->max_size);

    // Clean up any empty directories in cache. This may happen if the component stops running while
    // it is deleting a snapshot.
    RemoveEmptyDirectories(cache_metadata_->RootDir());
    cache_metadata_->RecreateFromFilesystem();
  }
}

bool SnapshotPersistence::Add(const SnapshotUuid& uuid, const ManagedSnapshot::Archive& archive,
                              StorageSize archive_size, const bool only_consider_tmp) {
  if (!SnapshotPersistenceEnabled()) {
    return false;
  }

  FX_CHECK(!Contains(uuid)) << "Duplicate snapshot uuid '" << uuid << "' added to persistence";

  SnapshotPersistenceMetadata* root_metadata = PickRootForStorage(archive_size, only_consider_tmp);

  if (root_metadata == nullptr) {
    FX_LOGS(ERROR) << "Failed to add snapshot to persistence; snapshot storage limits reached";
    return false;
  }

  return AddToRoot(uuid, archive, archive_size, *root_metadata);
}

bool SnapshotPersistence::AddToRoot(const SnapshotUuid& uuid,
                                    const ManagedSnapshot::Archive& archive,
                                    StorageSize archive_size, SnapshotPersistenceMetadata& root) {
  // Delete the persisted files and attempt to store the report under a new directory.
  auto on_error = [this, &uuid, &archive, archive_size,
                   &root](const std::optional<std::string>& snapshot_dir) {
    if (snapshot_dir.has_value()) {
      DeletePath(*snapshot_dir);
    }

    if (!HasFallbackRoot(root)) {
      return false;
    }

    auto& fallback_root = FallbackRoot(root);
    FX_LOGS(INFO) << "Using fallback root: " << fallback_root.RootDir();

    return AddToRoot(uuid, archive, archive_size, fallback_root);
  };

  // Ensure there's enough space in the store for the snapshot.
  if (!SpaceAvailable(root, archive_size)) {
    FX_LOGS(ERROR) << "No space left for snapshot in '" << root.RootDir() << "'";
    return on_error(std::nullopt);
  }

  const std::string snapshot_dir = files::JoinPath(root.RootDir(), uuid);
  if (!files::CreateDirectory(snapshot_dir)) {
    FX_LOGS(ERROR) << "Failed to create directory for snapshot: " << uuid;
    return on_error(std::nullopt);
  }

  // Write the archive to the the filesystem.
  const std::string archive_path = files::JoinPath(snapshot_dir, archive.key);
  if (!WriteData(archive_path, archive.value)) {
    FX_LOGS(ERROR) << "Failed to write to '" << archive_path << "'";
    return on_error(snapshot_dir);
  }

  root.Add(uuid, archive_size, archive.key);

  return true;
}

void SnapshotPersistence::MoveToTmp(const SnapshotUuid& uuid) {
  FX_CHECK(SnapshotPersistenceEnabled()) << "Snapshot persistence not enabled";
  FX_CHECK(SnapshotLocation(uuid) == ItemLocation::kCache)
      << "MoveToTmp() will only move snapshots from /cache to /tmp";

  const auto snapshot = Get(uuid);
  const StorageSize snapshot_size = cache_metadata_->SnapshotSize(uuid);

  // Delete copy of snapshot from /cache before adding to /tmp to avoid the possibility of having
  // the snapshot in multiple places if deletion from /cache were to fail.
  if (!DeletePath(cache_metadata_->SnapshotDirectory(uuid))) {
    FX_LOGS(ERROR) << "Failed to delete snapshot at " << cache_metadata_->SnapshotDirectory(uuid);
    return;
  }

  cache_metadata_->Delete(uuid);

  if (!tmp_metadata_.has_value() || !tmp_metadata_->IsDirectoryUsable() ||
      !SpaceAvailable(*tmp_metadata_, snapshot_size) ||
      !AddToRoot(uuid, *snapshot, snapshot_size, *tmp_metadata_)) {
    FX_LOGS(ERROR) << "Failed to move snapshot uuid '" << uuid << "' from /cache to /tmp";
  }
}

bool SnapshotPersistence::Contains(const SnapshotUuid& uuid) {
  // This is done here because it is a natural synchronization point and any operation acting on a
  // snapshot must call Contains or SnapshotLocation in order to safely proceed.
  SyncWithFilesystem(uuid);

  return (tmp_metadata_.has_value() && tmp_metadata_->Contains(uuid)) ||
         (cache_metadata_.has_value() && cache_metadata_->Contains(uuid));
}

std::optional<ItemLocation> SnapshotPersistence::SnapshotLocation(const SnapshotUuid& uuid) {
  // Call Contains to first sync with the filesystem.
  if (!Contains(uuid)) {
    return std::nullopt;
  }

  if (tmp_metadata_.has_value() && tmp_metadata_->Contains(uuid)) {
    return ItemLocation::kTmp;
  }

  if (cache_metadata_.has_value() && cache_metadata_->Contains(uuid)) {
    return ItemLocation::kCache;
  }

  return std::nullopt;
}

std::shared_ptr<const ManagedSnapshot::Archive> SnapshotPersistence::Get(const SnapshotUuid& uuid) {
  FX_CHECK(SnapshotPersistenceEnabled()) << "Snapshot persistence not enabled";
  FX_CHECK(Contains(uuid)) << "Contains() should be called before any Get()";

  const auto& root_metadata = RootFor(uuid);
  const auto snapshot_dir = root_metadata.SnapshotDirectory(uuid);
  const auto snapshot_filename = root_metadata.SnapshotKey(uuid);

  SizedData archive;
  if (!ReadSnapshot(files::JoinPath(snapshot_dir, snapshot_filename), &archive)) {
    FX_LOGS(FATAL) << "Failed to read snapshot for uuid '" << uuid << "'";
  }

  return std::make_shared<ManagedSnapshot::Archive>(snapshot_filename, std::move(archive));
}

std::vector<SnapshotUuid> SnapshotPersistence::GetSnapshotUuids() const {
  if (!SnapshotPersistenceEnabled()) {
    return {};
  }

  auto all_uuids =
      tmp_metadata_.has_value() ? tmp_metadata_->SnapshotUuids() : std::vector<SnapshotUuid>();
  const auto cache_uuids =
      cache_metadata_.has_value() ? cache_metadata_->SnapshotUuids() : std::vector<SnapshotUuid>();

  all_uuids.insert(all_uuids.end(), cache_uuids.begin(), cache_uuids.end());
  return all_uuids;
}

bool SnapshotPersistence::Delete(const SnapshotUuid& uuid) {
  FX_CHECK(SnapshotPersistenceEnabled()) << "Snapshot persistence not enabled";
  FX_CHECK(Contains(uuid)) << "Contains() should be called before any Delete()";

  auto& root_metadata = RootFor(uuid);
  if (!DeletePath(root_metadata.SnapshotDirectory(uuid))) {
    FX_LOGS(ERROR) << "Failed to delete snapshot at " << root_metadata.SnapshotDirectory(uuid);
    return false;
  }

  root_metadata.Delete(uuid);

  return true;
}

void SnapshotPersistence::DeleteAll() {
  auto DeleteAll = [](const std::string& root_dir) {
    if (!DeletePath(root_dir)) {
      FX_LOGS(ERROR) << "Failed to delete all snapshots from " << root_dir;
    }
    files::CreateDirectory(root_dir);
  };

  // /tmp must be usable if snapshot persistence is enabled there.
  if (tmp_metadata_.has_value()) {
    DeleteAll(tmp_metadata_->RootDir());
    FX_CHECK(tmp_metadata_->RecreateFromFilesystem());
  }

  if (cache_metadata_.has_value() && cache_metadata_->IsDirectoryUsable()) {
    DeleteAll(cache_metadata_->RootDir());
    cache_metadata_->RecreateFromFilesystem();
  }
}

SnapshotPersistenceMetadata& SnapshotPersistence::RootFor(const SnapshotUuid& uuid) {
  FX_CHECK(SnapshotPersistenceEnabled()) << "Snapshot persistence not enabled";

  if (tmp_metadata_.has_value() && tmp_metadata_->Contains(uuid)) {
    return *tmp_metadata_;
  }

  if (!cache_metadata_.has_value() || !cache_metadata_->Contains(uuid)) {
    FX_LOGS(FATAL) << "Unable to find root for uuid '" << uuid
                   << "', there's a logic bug somewhere";
  }

  return *cache_metadata_;
}

SnapshotPersistenceMetadata* SnapshotPersistence::PickRootForStorage(StorageSize archive_size,
                                                                     const bool only_consider_tmp) {
  FX_CHECK(SnapshotPersistenceEnabled()) << "Snapshot persistence not enabled";

  // Attempt to make |cache_metadata_| usable if it isn't already.
  if (cache_metadata_.has_value() && !cache_metadata_->IsDirectoryUsable()) {
    cache_metadata_->RecreateFromFilesystem();
  }

  // Only use a root if it's valid and there's enough space to put the archive there. Don't use
  // /cache if |only_consider_tmp| is true.
  if (cache_metadata_.has_value() && !only_consider_tmp && cache_metadata_->IsDirectoryUsable() &&
      SpaceAvailable(*cache_metadata_, archive_size)) {
    return &cache_metadata_.value();
  }

  if (tmp_metadata_.has_value() && tmp_metadata_->IsDirectoryUsable() &&
      SpaceAvailable(*tmp_metadata_, archive_size)) {
    return &tmp_metadata_.value();
  }

  return nullptr;
}

bool SnapshotPersistence::HasFallbackRoot(const SnapshotPersistenceMetadata& root) const {
  FX_CHECK(SnapshotPersistenceEnabled()) << "Snapshot persistence not enabled";

  // Only /cache can fallback.
  return cache_metadata_.has_value() && &root == &cache_metadata_.value() &&
         tmp_metadata_.has_value();
}

SnapshotPersistenceMetadata& SnapshotPersistence::FallbackRoot(
    const SnapshotPersistenceMetadata& root) {
  FX_CHECK(SnapshotPersistenceEnabled()) << "Snapshot persistence not enabled";
  FX_CHECK(HasFallbackRoot(root));

  // Always fallback to /tmp.
  return *tmp_metadata_;
}

bool SnapshotPersistence::SnapshotPersistenceEnabled() const {
  return tmp_metadata_.has_value() || cache_metadata_.has_value();
}

void SnapshotPersistence::SyncWithFilesystem(const SnapshotUuid& uuid) {
  if (tmp_metadata_.has_value() && tmp_metadata_->Contains(uuid) &&
      !files::IsDirectory(tmp_metadata_->SnapshotDirectory(uuid))) {
    tmp_metadata_->Delete(uuid);
  }

  if (cache_metadata_.has_value() && cache_metadata_->Contains(uuid) &&
      !files::IsDirectory(cache_metadata_->SnapshotDirectory(uuid))) {
    cache_metadata_->Delete(uuid);
  }
}

}  // namespace forensics::crash_reports
