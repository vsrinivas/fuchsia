// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/snapshot_persistence.h"

#include <utility>

#include "src/developer/forensics/crash_reports/snapshot_persistence_metadata.h"
#include "src/developer/forensics/utils/sized_data.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"

namespace forensics::crash_reports {

namespace {

bool ReadSnapshot(const std::string& path, SizedData* snapshot) {
  return files::ReadFileToVector(path, snapshot);
}

}  // namespace

SnapshotPersistence::SnapshotPersistence(std::string temp_dir, std::string persistent_dir)
    : tmp_metadata_(std::move(temp_dir)), cache_metadata_(std::move(persistent_dir)) {}

bool SnapshotPersistence::Contains(const SnapshotUuid& uuid) const {
  return tmp_metadata_.Contains(uuid) || cache_metadata_.Contains(uuid);
}

std::shared_ptr<const ManagedSnapshot::Archive> SnapshotPersistence::Get(const SnapshotUuid& uuid) {
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

SnapshotPersistenceMetadata& SnapshotPersistence::RootFor(const SnapshotUuid& uuid) {
  if (tmp_metadata_.Contains(uuid)) {
    return tmp_metadata_;
  }

  if (!cache_metadata_.Contains(uuid)) {
    FX_LOGS(FATAL) << "Unable to find root for uuid '" << uuid
                   << "', there's a logic bug somewhere";
  }

  return cache_metadata_;
}

}  // namespace forensics::crash_reports
