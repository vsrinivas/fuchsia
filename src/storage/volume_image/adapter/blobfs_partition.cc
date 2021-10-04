// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/adapter/blobfs_partition.h"

#include <lib/fpromise/result.h>
#include <zircon/hw/gpt.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

#include <safemath/safe_conversions.h>

#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/fvm/format.h"
#include "src/storage/volume_image/address_descriptor.h"
#include "src/storage/volume_image/options.h"
#include "src/storage/volume_image/utils/block_utils.h"
#include "src/storage/volume_image/utils/guid.h"
#include "src/storage/volume_image/utils/reader.h"
#include "src/storage/volume_image/volume_descriptor.h"

namespace storage::volume_image {

namespace {

// Expected label for blobfs volume
constexpr std::string_view kBlobfsLabel = "blobfs";

// Expected GUID for blobfs instance.
constexpr std::array<uint8_t, kGuidLength> kBlobfsTypeGuid = GUID_BLOB_VALUE;

// The FVM version of Blobfs has an extra block after the superblock, which is the backup
// superblock. This reader 'injects' a copy of the superblock on the block following the superblock.
// This is the backup superblock.
class BackupSuperblockReader final : public Reader {
 public:
  explicit BackupSuperblockReader(std::unique_ptr<Reader> reader) : reader_(std::move(reader)) {}

  uint64_t length() const final { return reader_->length() + blobfs::kBlobfsBlockSize; }

  fpromise::result<void, std::string> Read(uint64_t offset,
                                           cpp20::span<uint8_t> buffer) const final {
    if (offset + buffer.size() > blobfs::kBlobfsBlockSize) {
      // Read the first part with the original offset if it crosses the boundary.
      if (offset < blobfs::kBlobfsBlockSize) {
        uint64_t non_adjusted_bytes = blobfs::kBlobfsBlockSize - offset;
        if (auto read_result = reader_->Read(offset, buffer.subspan(0, non_adjusted_bytes));
            read_result.is_error()) {
          return read_result.take_error_result();
        }
        // Now update the buffer view.
        buffer = buffer.subspan(non_adjusted_bytes);
        offset += non_adjusted_bytes;
      }
      offset -= blobfs::kBlobfsBlockSize;
    }
    // The first superblock does not need any adjustment.
    return reader_->Read(offset, buffer);
  };

 private:
  std::unique_ptr<Reader> reader_;
};

// For blobfs we need to replace contents from the superblock, to make it look like its fvm based
// blobfs.
class PatchedSuperblockReader final : public Reader {
 public:
  explicit PatchedSuperblockReader(std::unique_ptr<Reader> reader) : reader_(std::move(reader)) {}

  uint64_t length() const final { return reader_->length(); }

  fpromise::result<void, std::string> Read(uint64_t offset,
                                           cpp20::span<uint8_t> buffer) const final {
    if (auto read_result = reader_->Read(offset, buffer); read_result.is_error()) {
      return read_result.take_error_result();
    }
    if (offset < blobfs::kBlobfsBlockSize) {
      uint64_t content_bytes =
          std::min(static_cast<uint64_t>(buffer.size()), blobfs::kBlobfsBlockSize - offset);
      memset(buffer.data(), 0, content_bytes);
      if (offset < sizeof(blobfs::Superblock)) {
        uint64_t content_offset = offset % sizeof(blobfs::Superblock);
        uint64_t remaining_bytes = std::min(
            buffer.size(), static_cast<size_t>(sizeof(blobfs::Superblock) - content_offset));
        memcpy(buffer.data(), reinterpret_cast<const uint8_t*>(&superblock_) + content_offset,
               remaining_bytes);
      }
    }
    return fpromise::ok();
  };

  blobfs::Superblock& superblock() { return superblock_; }

 private:
  blobfs::Superblock superblock_;
  std::unique_ptr<Reader> reader_;
};

}  // namespace

fpromise::result<Partition, std::string> CreateBlobfsFvmPartition(
    std::unique_ptr<Reader> source_image, const PartitionOptions& partition_options,
    const FvmOptions& fvm_options) {
  if (fvm_options.slice_size % blobfs::kBlobfsBlockSize != 0) {
    return fpromise::error(
        "Fvm slice size must be a multiple of blobfs block size. Expected blobfs_block_size: " +
        std::to_string(blobfs::kBlobfsBlockSize) +
        " fvm_slice_size: " + std::to_string(fvm_options.slice_size) + ".");
  }

  if (2 * blobfs::kBlobfsBlockSize > fvm_options.slice_size) {
    return fpromise::error(
        "Blobfs Superblock and Backup Superblock must fit within the first slice. Expected slice "
        "size of at least " +
        std::to_string(2 * blobfs::kBlobfsBlockSize) + ", but found " +
        std::to_string(fvm_options.slice_size) + ".");
  }

  // Load blobfs superblock to obtain extent sizes and such.
  blobfs::Superblock superblock = {};
  if (auto sb_read_result =
          source_image->Read(0, cpp20::span<uint8_t>(reinterpret_cast<uint8_t*>(&superblock),
                                                     sizeof(blobfs::Superblock)));
      sb_read_result.is_error()) {
    return sb_read_result.take_error_result();
  }

  // Minor validation that we are actually dealing with a blobfs superblock.
  if (superblock.magic0 != blobfs::kBlobfsMagic0) {
    return fpromise::error(
        "Found bad magic0(" + std::to_string(superblock.magic0) +
        ") value in blobfs superblock(Expected: " + std::to_string(blobfs::kBlobfsMagic0) + ").");
  }

  if (superblock.magic1 != blobfs::kBlobfsMagic1) {
    return fpromise::error(
        "Found bad magic1(" + std::to_string(superblock.magic1) +
        ") value in blobfs superblock(Expected: " + std::to_string(blobfs::kBlobfsMagic1) + ").");
  }

  // Helper to calculate slice count.
  auto get_slice_count = [&fvm_options](const auto& mapping) {
    auto extent_size = std::max(mapping.count, mapping.size.value_or(0));
    return GetBlockCount(mapping.target, extent_size, fvm_options.slice_size);
  };

  uint64_t accumulated_slices = 0;

  VolumeDescriptor volume;
  volume.block_size = blobfs::kBlobfsBlockSize;
  volume.size = source_image->length();
  volume.encryption = EncryptionType::kNone;
  volume.name = kBlobfsLabel;
  memcpy(volume.type.data(), kBlobfsTypeGuid.data(), volume.type.size());
  memcpy(volume.instance.data(), fvm::kPlaceHolderInstanceGuid.data(), volume.instance.size());

  AddressDescriptor address;

  // Currently there is a limitation on the host tool, since it meets the existing requirements and
  // simplifies the process.
  //
  // That is, mappings do not share slices. Which is why, instead of mapping the superblock
  // to two different target offsets we need to use a wrapper on reader.
  AddressMap superblock_mapping;
  superblock_mapping.source = 0;
  superblock_mapping.count = 2 * blobfs::kBlobfsBlockSize;
  superblock_mapping.target = 0;
  superblock_mapping.options[EnumAsString(AddressMapOption::kFill)] = 0;
  address.mappings.push_back(superblock_mapping);
  accumulated_slices += get_slice_count(superblock_mapping);

  // All blocks below this, need to account for an extra block inserted at runtime, which is the
  // backup superblock.
  uint64_t min_data_blocks =
      GetBlockCount(blobfs::kFVMDataStart, partition_options.min_data_bytes.value_or(0),
                    blobfs::kBlobfsBlockSize);

  AddressMap block_map_mapping;
  block_map_mapping.source =
      (blobfs::BlockMapStartBlock(superblock) + 1) * blobfs::kBlobfsBlockSize;
  block_map_mapping.target = blobfs::kFVMBlockMapStart * blobfs::kBlobfsBlockSize;
  block_map_mapping.count =
      blobfs::BlocksRequiredForBits(superblock.data_block_count) * blobfs::kBlobfsBlockSize;
  block_map_mapping.size =
      std::max(block_map_mapping.count,
               static_cast<uint64_t>(blobfs::BlocksRequiredForBits(min_data_blocks) *
                                     blobfs::kBlobfsBlockSize));
  block_map_mapping.options[EnumAsString(AddressMapOption::kFill)] = 0;
  address.mappings.push_back(block_map_mapping);
  accumulated_slices += get_slice_count(block_map_mapping);

  AddressMap inode_mapping;
  inode_mapping.source = (blobfs::NodeMapStartBlock(superblock) + 1) * blobfs::kBlobfsBlockSize;
  inode_mapping.target = blobfs::kFVMNodeMapStart * blobfs::kBlobfsBlockSize;
  inode_mapping.count =
      blobfs::BlocksRequiredForInode(superblock.inode_count) * blobfs::kBlobfsBlockSize;
  inode_mapping.size = blobfs::BlocksRequiredForInode(std::max(
                           superblock.inode_count, partition_options.min_inode_count.value_or(0))) *
                       blobfs::kBlobfsBlockSize;
  inode_mapping.options[EnumAsString(AddressMapOption::kFill)] = 0;
  address.mappings.push_back(inode_mapping);
  accumulated_slices += get_slice_count(inode_mapping);

  AddressMap data_mapping;
  data_mapping.source = (blobfs::DataStartBlock(superblock) + 1) * blobfs::kBlobfsBlockSize;
  data_mapping.target = blobfs::kFVMDataStart * blobfs::kBlobfsBlockSize;
  data_mapping.count = superblock.data_block_count * blobfs::kBlobfsBlockSize;
  data_mapping.size = std::max(data_mapping.count, partition_options.min_data_bytes.value_or(0));
  address.mappings.push_back(data_mapping);
  accumulated_slices += get_slice_count(data_mapping);

  AddressMap journal_mapping;
  journal_mapping.source = (blobfs::JournalStartBlock(superblock) + 1) * blobfs::kBlobfsBlockSize;
  journal_mapping.target = blobfs::kFVMJournalStart * blobfs::kBlobfsBlockSize;
  journal_mapping.count = blobfs::JournalBlocks(superblock) * blobfs::kBlobfsBlockSize;
  accumulated_slices += get_slice_count(journal_mapping);

  // Add any leftover space to the journal.
  if (partition_options.max_bytes.has_value()) {
    uint64_t max_slices = partition_options.max_bytes.value() / fvm_options.slice_size;
    uint64_t available_slices =
        accumulated_slices > max_slices ? 0 : max_slices - accumulated_slices;

    // If there are more bytes available than the original image had reserved, increase the journal
    // size to match all remaining space.
    journal_mapping.size =
        (available_slices + get_slice_count(journal_mapping)) * fvm_options.slice_size;
  }
  address.mappings.push_back(journal_mapping);

  accumulated_slices = 0;
  for (const auto& mapping : address.mappings) {
    accumulated_slices += get_slice_count(mapping);
  }
  uint64_t accumulated_bytes = accumulated_slices * fvm_options.slice_size;

  if (partition_options.max_bytes.has_value() &&
      accumulated_bytes > partition_options.max_bytes.value()) {
    return fpromise::error("Blobfs FVM Partition allocated " + std::to_string(accumulated_slices) +
                           "(" + std::to_string(accumulated_bytes) +
                           " bytes) exceeding provided upperbound |max_bytes|(" +
                           std::to_string(partition_options.max_bytes.value()) + ").");
  }

  auto patched_superblock_reader =
      std::make_unique<PatchedSuperblockReader>(std::move(source_image));

  auto& patched_superblock = patched_superblock_reader->superblock();
  memcpy(&patched_superblock, &superblock, sizeof(blobfs::Superblock));
  patched_superblock.flags |= blobfs::kBlobFlagFVM;
  patched_superblock.inode_count = safemath::checked_cast<uint32_t>(
      get_slice_count(inode_mapping) * fvm_options.slice_size / blobfs::kBlobfsInodeSize);
  patched_superblock.journal_block_count =
      get_slice_count(journal_mapping) * fvm_options.slice_size / blobfs::kBlobfsBlockSize;
  patched_superblock.data_block_count =
      get_slice_count(data_mapping) * fvm_options.slice_size / blobfs::kBlobfsBlockSize;
  patched_superblock.slice_size = fvm_options.slice_size;
  patched_superblock.abm_slices = get_slice_count(block_map_mapping);
  patched_superblock.ino_slices = get_slice_count(inode_mapping);
  patched_superblock.dat_slices = get_slice_count(data_mapping);
  patched_superblock.journal_slices = get_slice_count(journal_mapping);

  auto reader_with_backup_superblock =
      std::make_unique<BackupSuperblockReader>(std::move(patched_superblock_reader));
  return fpromise::ok(Partition(volume, address, std::move(reader_with_backup_superblock)));
}

}  // namespace storage::volume_image
