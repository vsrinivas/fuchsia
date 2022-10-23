// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FVM_METADATA_H_
#define SRC_STORAGE_FVM_METADATA_H_

#include <lib/stdcompat/span.h>
#include <lib/zx/result.h>
#include <zircon/types.h>

#include <limits>

#include "src/storage/fvm/format.h"
#include "src/storage/fvm/metadata_buffer.h"

namespace fvm {

// Metadata is an in-memory representation of the metadata for an FVM image.
//
// At construction, |Metadata| objects are well-formed, since they validate the underlying metadata
// when first created by |Metadata::Create| or |Metadata::Synthesize|. Subsequent updates by clients
// can, of course, corrupt the metadata.
//
// This class owns the underlying buffer (see |MetadataBuffer)|.
//
// This class is not thread-safe.
class Metadata {
 public:
  // Constructs a default, uninitialized instance.
  Metadata() = default;
  Metadata(const Metadata&) = delete;
  Metadata& operator=(const Metadata&) = delete;
  Metadata(Metadata&&) noexcept;
  Metadata& operator=(Metadata&&) noexcept;

  // Returns the minimum number of bytes needed for a |MetadataBuffer| object to back FVM metadata
  // described by |header|.
  static size_t BytesNeeded(const Header& header);

  // Attempts to parse the FVM metadata stored at |data_a| and |data_b|, picking the latest copy.
  // The copy with the latest generation (that is also valid) will be retained; the other is
  // discarded.
  // Returns a |Metadata| instance over the passed metadata on success, or a failure if neither was
  // valid.
  static zx::result<Metadata> Create(std::unique_ptr<MetadataBuffer> data_a,
                                     std::unique_ptr<MetadataBuffer> data_b);

  // Override of |Create| that allows specifying disk dimensions; the sizes of each metadata copy
  // will be checked against these sizes (see |::fvm::PickValidHeader|) and only deemed valid if
  // they fit within the disk.
  static zx::result<Metadata> Create(size_t disk_size, size_t disk_block_size,
                                     std::unique_ptr<MetadataBuffer> data_a,
                                     std::unique_ptr<MetadataBuffer> data_b);

  // Creates an instance of |Metadata|, initialized by copying the contents of |header|,
  // |partitions| and |slices|.
  // All of the passed metadata is copied into both the A and B slots. Any additional partitions
  // and slices in the tables past |partitions| and |slices| are default-initialized.
  // The passed |header| must be configured appropriately to manage tables at least as big as
  // |num_partitions| and |num_slices| respectively. If not, an error is returned.
  //
  // Note: The user has no need to concern themselves with reserved pslice zero, since this is
  //       adjusted internally. Though, all vpartitions' indexes must be in the range [1,
  //       VPartitionEntryCount).
  static zx::result<Metadata> Synthesize(const fvm::Header& header,
                                         const VPartitionEntry* partitions, size_t num_partitions,
                                         const SliceEntry* slices, size_t num_slices);

  // See Synthesize(fvm::Header, const VPartition,* size_t, const SliceEntry*, size_t);
  static zx::result<Metadata> Synthesize(const fvm::Header& header,
                                         cpp20::span<const VPartitionEntry> vpartitions,
                                         cpp20::span<const SliceEntry> slices) {
    return Synthesize(header, vpartitions.data(), vpartitions.size(), slices.data(), slices.size());
  }

  // Checks the validity of the metadata. The underlying device's information is passed in, see
  // fvm::Header::IsValid(). The defaults for the disk information skips validation of the metadata
  // relative to these values.
  //
  // Should be called before serializing the contents to disk.
  bool CheckValidity(uint64_t disk_size = std::numeric_limits<uint64_t>::max(),
                     uint64_t disk_block_size = kBlockSize) const;

  // Updates the hash stored in the metadata, based on its contents.
  void UpdateHash();

  // Returns the disk offset where the metadata should be persisted. This points to the
  // offset of the *inactive* copy (see |inactive_header()|).
  size_t GetInactiveHeaderOffset() const;

  // Returns whether the Metadata represents an active A copy or B copy.
  SuperblockType active_header() const { return active_header_; }
  SuperblockType inactive_header() const { return OppositeHeader(active_header_); }

  // Switches whether the Metadata represents an active A or B copy.
  void SwitchActiveHeaders();

  // Accesses the header managed by the Metadata instance.
  Header& GetHeader() const;

  // Accesses the partition table. Note that |idx| is one-based.
  VPartitionEntry& GetPartitionEntry(size_t idx) const;

  // Accesses the allocation table. Note that |idx| is one-based.
  SliceEntry& GetSliceEntry(size_t idx) const;

  // Gets a view of the raw metadata buffer.
  const MetadataBuffer* Get() const;

  // Creates a copy of this Metadata instance, with additional room described by |dimensions|.
  // The metadata is not copied verbatim; for instance, which of the A/B copies is active
  // may change, and old generations may be lost. The only guarantee is that all partition/slice
  // entries in the active tables will be copied over from this instance.
  // TODO(jfsulliv): Only fvm/host needs this method, and it is not very graceful. Consider removal.
  zx::result<Metadata> CopyWithNewDimensions(const Header& dimensions) const;

 private:
  Metadata(std::unique_ptr<MetadataBuffer> data, SuperblockType active_header);

  constexpr static SuperblockType OppositeHeader(SuperblockType type) {
    switch (type) {
      case SuperblockType::kPrimary:
        return SuperblockType::kSecondary;
      case SuperblockType::kSecondary:
        return SuperblockType::kPrimary;
    }
  }

  void MoveFrom(Metadata&& o);

  size_t MetadataOffset(SuperblockType type) const;

  std::unique_ptr<MetadataBuffer> data_;
  SuperblockType active_header_{SuperblockType::kPrimary};
};

}  // namespace fvm

#endif  // SRC_STORAGE_FVM_METADATA_H_
