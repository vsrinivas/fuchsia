// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FVM_SNAPSHOT_METADATA_H_
#define SRC_STORAGE_FVM_SNAPSHOT_METADATA_H_

#include <lib/zx/status.h>
#include <zircon/types.h>

#include <memory>
#include <optional>

#include "src/storage/fvm/metadata_buffer.h"
#include "src/storage/fvm/snapshot_metadata_format.h"

namespace fvm {

// SnapshotMetadata is an in-memory representation of the snapshot metadata for an FVM image.
//
// At construction, |SnapshotMetadata| objects are well-formed, since they validate the underlying
// metadata when first created by |SnapshotMetadata::Create| or |SnapshotMetadata::Synthesize|.
// Subsequent updates by clients can, of course, corrupt the metadata.
//
// This class owns the underlying buffer (see |MetadataBuffer)|.
//
// This class is not thread-safe.
class SnapshotMetadata {
 public:
  // Constructs a default, uninitialized instance.
  SnapshotMetadata() = default;
  SnapshotMetadata(const SnapshotMetadata&) = delete;
  SnapshotMetadata& operator=(const SnapshotMetadata&) = delete;
  SnapshotMetadata(SnapshotMetadata&&) noexcept;
  SnapshotMetadata& operator=(SnapshotMetadata&&) noexcept;

  // Returns the minimum number of bytes needed for a |MetadataBuffer| object to back FVM snapshot
  // metadata described by |header|.
  static size_t BytesNeeded(const SnapshotMetadataHeader& header);

  // Attempts to parse the FVM metadata stored at |data_a| and |data_b|, picking the latest copy.
  // The copy with the latest generation (that is also valid) will be retained; the other is
  // discarded.
  // Returns a |SnapshotMetadata| instance over the passed metadata on success, or a failure if
  // neither was valid.
  static zx::status<SnapshotMetadata> Create(std::unique_ptr<MetadataBuffer> data_a,
                                             std::unique_ptr<MetadataBuffer> data_b);

  // Creates an instance of |SnapshotMetadata|, initialized by copying the contents of |partitions|
  // and |extents|.
  // All of the passed metadata is copied into both the A and B slots. Any additional partitions
  // and slices in the tables past |partitions| and |extents| are default-initialized.
  static zx::status<SnapshotMetadata> Synthesize(const PartitionSnapshotState* partitions,
                                                 size_t num_partitions,
                                                 const SnapshotExtentType* extents,
                                                 size_t num_extents);

  // Picks the valid copy stored in |a| and |b| that has the greatest generation number.
  static std::optional<SnapshotMetadataCopy> PickValid(const void* a, const void* b,
                                                       size_t meta_size);

  // Updates the hash stored in the metadata, based on its contents.
  void UpdateHash();

  // Returns the disk offset where the metadata should be persisted. This points to the
  // offset of the *inactive* copy (see |inactive_header()|).
  size_t GetInactiveHeaderOffset() const;

  // Returns whether the Metadata represents an active A copy or B copy.
  SnapshotMetadataCopy active_header() const { return active_header_; }
  SnapshotMetadataCopy inactive_header() const { return OppositeHeader(active_header_); }

  // Switches whether the Metadata represents an active A or B copy.
  void SwitchActiveHeaders();

  // Accesses the header managed by the Metadata instance.
  SnapshotMetadataHeader& GetHeader() const;

  // Accesses the partition state table. Note that |idx| is one-based.
  // |idx| is the same as the index in the main partition table.
  PartitionSnapshotState& GetPartitionStateEntry(size_t idx) const;

  // Accesses the extent type table.
  SnapshotExtentType& GetExtentTypeEntry(size_t idx) const;

  // Gets a view of the raw metadata buffer.
  const MetadataBuffer* Get() const;

 private:
  SnapshotMetadata(std::unique_ptr<MetadataBuffer> data, SnapshotMetadataCopy active_header);

  static bool CheckHash(const void* metadata, size_t meta_size);

  constexpr static SnapshotMetadataCopy OppositeHeader(SnapshotMetadataCopy type) {
    switch (type) {
      case SnapshotMetadataCopy::kPrimary:
        return SnapshotMetadataCopy::kSecondary;
      case SnapshotMetadataCopy::kSecondary:
        return SnapshotMetadataCopy::kPrimary;
    }
  }

  void MoveFrom(SnapshotMetadata&& o);

  std::unique_ptr<MetadataBuffer> data_;
  SnapshotMetadataCopy active_header_{SnapshotMetadataCopy::kPrimary};
};

}  // namespace fvm

#endif  // SRC_STORAGE_FVM_SNAPSHOT_METADATA_H_
