// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/snapshot_store.h"

#include <lib/stdcompat/internal/erase.h>

#include <fstream>

#include "src/developer/forensics/crash_reports/constants.h"
#include "src/developer/forensics/crash_reports/snapshot.h"

namespace forensics::crash_reports {
namespace {

// Helper function to make a shared_ptr from a rvalue-reference of a type.
template <typename T>
std::shared_ptr<T> MakeShared(T&& t) {
  return std::make_shared<T>(static_cast<std::remove_reference_t<T>&&>(t));
}

}  // namespace

SnapshotStore::SnapshotStore(feedback::AnnotationManager* annotation_manager,
                             std::string garbage_collected_snapshots_path,
                             StorageSize max_archives_size)
    : annotation_manager_(annotation_manager),
      garbage_collected_snapshots_path_(std::move(garbage_collected_snapshots_path)),
      max_archives_size_(max_archives_size),
      current_archives_size_(0u),
      garbage_collected_snapshot_(kGarbageCollectedSnapshotUuid,
                                  feedback::Annotations({
                                      {"debug.snapshot.error", "garbage collected"},
                                      {"debug.snapshot.present", "false"},
                                  })),
      not_persisted_snapshot_(kNotPersistedSnapshotUuid,
                              feedback::Annotations({
                                  {"debug.snapshot.error", "not persisted"},
                                  {"debug.snapshot.present", "false"},
                              })),
      timed_out_snapshot_(kTimedOutSnapshotUuid, feedback::Annotations({
                                                     {"debug.snapshot.error", "timeout"},
                                                     {"debug.snapshot.present", "false"},
                                                 })),
      shutdown_snapshot_(kShutdownSnapshotUuid, feedback::Annotations({
                                                    {"debug.snapshot.error", "system shutdown"},
                                                    {"debug.snapshot.present", "false"},
                                                })),
      no_uuid_snapshot_(kNoUuidSnapshotUuid, feedback::Annotations({
                                                 {"debug.snapshot.error", "missing uuid"},
                                                 {"debug.snapshot.present", "false"},
                                             })) {
  // Load the file lines into a set of UUIDs.
  std::ifstream file(garbage_collected_snapshots_path_);
  for (std::string uuid; getline(file, uuid);) {
    garbage_collected_snapshots_.insert(uuid);
  }
}

Snapshot SnapshotStore::GetSnapshot(const SnapshotUuid& uuid) {
  auto BuildMissing = [this](const SpecialCaseSnapshot& special_case) {
    return MissingSnapshot(annotation_manager_->ImmediatelyAvailable(), special_case.annotations);
  };

  if (uuid == kGarbageCollectedSnapshotUuid) {
    return BuildMissing(garbage_collected_snapshot_);
  }

  if (uuid == kNotPersistedSnapshotUuid) {
    return BuildMissing(not_persisted_snapshot_);
  }

  if (uuid == kTimedOutSnapshotUuid) {
    return BuildMissing(timed_out_snapshot_);
  }

  if (uuid == kShutdownSnapshotUuid) {
    return BuildMissing(shutdown_snapshot_);
  }

  if (uuid == kNoUuidSnapshotUuid) {
    return BuildMissing(no_uuid_snapshot_);
  }

  auto* data = FindSnapshotData(uuid);

  if (!data) {
    if (garbage_collected_snapshots_.find(uuid) != garbage_collected_snapshots_.end()) {
      return BuildMissing(garbage_collected_snapshot_);
    }
    return BuildMissing(not_persisted_snapshot_);
  }

  return ManagedSnapshot(data->archive);
}

MissingSnapshot SnapshotStore::GetMissingSnapshot(const SnapshotUuid& uuid) {
  const auto snapshot = GetSnapshot(uuid);
  FX_CHECK(std::holds_alternative<MissingSnapshot>(snapshot));

  return std::get<MissingSnapshot>(snapshot);
}

void SnapshotStore::DeleteSnapshot(const SnapshotUuid& uuid) {
  auto* data = FindSnapshotData(uuid);

  // The snapshot was likely dropped due to size constraints.
  if (!data) {
    return;
  }

  // TODO(fxbug.dev/102479): drop from persistence instead if that's where the snapshot is located.
  DropArchive(data);
  RecordAsGarbageCollected(uuid);
  data_.erase(uuid);
  cpp20::internal::remove_then_erase(insertion_order_, uuid);
}

void SnapshotStore::AddSnapshot(const SnapshotUuid& uuid, fuchsia::feedback::Attachment archive) {
  auto& data = data_[uuid];

  if (!archive.key.empty() && archive.value.vmo.is_valid()) {
    data.archive_size += StorageSize::Bytes(archive.key.size());
    data.archive_size += StorageSize::Bytes(archive.value.size);
    current_archives_size_ += data.archive_size;

    data.archive = MakeShared(ManagedSnapshot::Archive(archive));
  }

  insertion_order_.push_back(uuid);

  while (!insertion_order_.empty() && SizeLimitsExceeded()) {
    FX_CHECK(SnapshotExists(insertion_order_.front()))
        << "Snapshot for uuid " << insertion_order_.front() << " doesn't exist";

    EnforceSizeLimits(insertion_order_.front());
    insertion_order_.pop_front();
  }
}

void SnapshotStore::EnforceSizeLimits(const SnapshotUuid& uuid) {
  auto* data = FindSnapshotData(uuid);
  FX_CHECK(data);

  // Drop |data| if necessary.
  if (current_archives_size_ > max_archives_size_) {
    DropArchive(data);
    RecordAsGarbageCollected(uuid);
    data_.erase(uuid);
  }
}

bool SnapshotStore::SnapshotExists(const SnapshotUuid& uuid) {
  auto* data = FindSnapshotData(uuid);
  return data != nullptr;
}

size_t SnapshotStore::Size() const { return data_.size(); }

bool SnapshotStore::IsGarbageCollected(const SnapshotUuid& uuid) const {
  return garbage_collected_snapshots_.find(uuid) != garbage_collected_snapshots_.end();
}

bool SnapshotStore::SizeLimitsExceeded() const {
  return current_archives_size_ > max_archives_size_;
}

void SnapshotStore::DropArchive(SnapshotData* data) {
  data->archive = nullptr;

  current_archives_size_ -= data->archive_size;
  data->archive_size = StorageSize::Bytes(0u);
}

void SnapshotStore::RecordAsGarbageCollected(const SnapshotUuid& uuid) {
  if (garbage_collected_snapshots_.find(uuid) != garbage_collected_snapshots_.end()) {
    return;
  }

  garbage_collected_snapshots_.insert(uuid);

  // Append the UUID to the file on its own line.
  std::ofstream file(garbage_collected_snapshots_path_, std::ofstream::out | std::ofstream::app);
  file << uuid << "\n";
  file.close();
}

SnapshotStore::SnapshotData* SnapshotStore::FindSnapshotData(const SnapshotUuid& uuid) {
  return (data_.find(uuid) == data_.end()) ? nullptr : &(data_.at(uuid));
}

}  // namespace forensics::crash_reports
