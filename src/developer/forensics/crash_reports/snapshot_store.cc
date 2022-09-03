// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/snapshot_store.h"

#include <fstream>

#include "src/developer/forensics/crash_reports/constants.h"

namespace forensics::crash_reports {
namespace {

using fuchsia::feedback::Annotation;
using fuchsia::feedback::GetSnapshotParameters;

template <typename V>
void AddAnnotation(const std::string& key, const V& value, feedback::Annotations& annotations) {
  annotations.insert({key, std::to_string(value)});
}

template <>
void AddAnnotation<std::string>(const std::string& key, const std::string& value,
                                feedback::Annotations& annotations) {
  annotations.insert({key, value});
}

// Helper function to make a shared_ptr from a rvalue-reference of a type.
template <typename T>
std::shared_ptr<T> MakeShared(T&& t) {
  return std::make_shared<T>(static_cast<std::remove_reference_t<T>&&>(t));
}

}  // namespace

SnapshotStore::SnapshotStore(feedback::AnnotationManager* annotation_manager,
                             std::string garbage_collected_snapshots_path,
                             StorageSize max_annotations_size, StorageSize max_archives_size)
    : annotation_manager_(annotation_manager),
      garbage_collected_snapshots_path_(std::move(garbage_collected_snapshots_path)),
      max_annotations_size_(max_annotations_size),
      current_annotations_size_(0u),
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

  return ManagedSnapshot(data->annotations, data->presence_annotations, data->archive);
}

void SnapshotStore::IncrementClientCount(const SnapshotUuid& uuid) {
  auto* data = FindSnapshotData(uuid);
  FX_CHECK(data);

  data->num_clients_with_uuid += 1;
}

bool SnapshotStore::Release(const SnapshotUuid& uuid) {
  if (uuid == kGarbageCollectedSnapshotUuid || uuid == kNotPersistedSnapshotUuid ||
      uuid == kTimedOutSnapshotUuid || uuid == kNoUuidSnapshotUuid) {
    return false;
  }

  auto* data = FindSnapshotData(uuid);

  // The snapshot was likely dropped due to size constraints.
  if (!data) {
    return false;
  }

  data->num_clients_with_uuid -= 1;

  // There are still clients that need the snapshot.
  if (data->num_clients_with_uuid > 0) {
    return false;
  }

  DropAnnotations(data);
  DropArchive(data);

  RecordAsGarbageCollected(uuid);
  data_.erase(uuid);
  return true;
}

void SnapshotStore::StartSnapshot(const SnapshotUuid& uuid) {
  data_.emplace(uuid, SnapshotData{
                          .num_clients_with_uuid = 0,
                          .annotations_size = StorageSize::Bytes(0u),
                          .archive_size = StorageSize::Bytes(0u),
                          .annotations = nullptr,
                          .archive = nullptr,
                          .presence_annotations = nullptr,
                      });
}

void SnapshotStore::AddSnapshotData(const SnapshotUuid& uuid, feedback::Annotations annotations,
                                    fuchsia::feedback::Attachment archive) {
  auto* data = FindSnapshotData(uuid);

  FX_CHECK(data);

  data->presence_annotations = std::make_shared<feedback::Annotations>();

  // Add annotations about the snapshot. These are not "presence" annotations because
  // they're unchanging and not the result of the SnapshotManager's data management.
  AddAnnotation("debug.snapshot.shared-request.num-clients", data->num_clients_with_uuid,
                annotations);
  AddAnnotation("debug.snapshot.shared-request.uuid", uuid, annotations);

  // Take ownership of |annotations| and then record the size of the annotations and archive.
  data->annotations = MakeShared(std::move(annotations));

  for (const auto& [k, v] : *data->annotations) {
    data->annotations_size += StorageSize::Bytes(k.size());
    if (v.HasValue()) {
      data->annotations_size += StorageSize::Bytes(v.Value().size());
    }
  }
  current_annotations_size_ += data->annotations_size;

  if (!archive.key.empty() && archive.value.vmo.is_valid()) {
    data->archive = MakeShared(ManagedSnapshot::Archive(archive));

    data->archive_size += StorageSize::Bytes(data->archive->key.size());
    data->archive_size += StorageSize::Bytes(data->archive->value.size());
    current_archives_size_ += data->archive_size;
  }

  if (data->archive == nullptr) {
    data->presence_annotations->insert({"debug.snapshot.present", "false"});
  }
}

void SnapshotStore::EnforceSizeLimits(const SnapshotUuid& uuid) {
  auto* data = FindSnapshotData(uuid);
  FX_CHECK(data);

  // Drop |data|'s annotations and attachments if necessary. Attachments are dropped because
  // they don't make sense without the accompanying annotations.
  if (current_annotations_size_ > max_annotations_size_) {
    DropAnnotations(data);
    DropArchive(data);
    RecordAsGarbageCollected(uuid);
  }

  // Drop |data|'s archive if necessary.
  if (current_archives_size_ > max_archives_size_) {
    DropArchive(data);
    RecordAsGarbageCollected(uuid);
  }

  // Delete the SnapshotRequest and SnapshotData if the annotations and archive have been
  // dropped, either in this iteration of the loop or a prior one.
  if (!data->annotations && !data->archive) {
    RecordAsGarbageCollected(uuid);
    data_.erase(uuid);
  }
}

bool SnapshotStore::SnapshotExists(const SnapshotUuid& uuid) {
  auto* data = FindSnapshotData(uuid);
  return data != nullptr;
}

bool SnapshotStore::SizeLimitsExceeded() const {
  return current_annotations_size_ > max_annotations_size_ ||
         current_archives_size_ > max_archives_size_;
}

void SnapshotStore::DropAnnotations(SnapshotData* data) {
  data->annotations = nullptr;
  data->presence_annotations = nullptr;

  current_annotations_size_ -= data->annotations_size;
  data->annotations_size = StorageSize::Bytes(0u);
}

void SnapshotStore::DropArchive(SnapshotData* data) {
  data->archive = nullptr;

  current_archives_size_ -= data->archive_size;
  data->archive_size = StorageSize::Bytes(0u);

  // If annotations still exist, add an annotation indicating the archive was garbage collected.
  if (data->annotations) {
    for (const auto& [k, v] : garbage_collected_snapshot_.annotations) {
      data->presence_annotations->insert({k, v});
      data->annotations_size += StorageSize::Bytes(k.size());
      current_annotations_size_ += StorageSize::Bytes(k.size());

      if (v.HasValue()) {
        data->annotations_size += StorageSize::Bytes(v.Value().size());
        current_annotations_size_ += StorageSize::Bytes(v.Value().size());
      }
    }
  }
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
