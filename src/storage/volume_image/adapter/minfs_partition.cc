// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/adapter/minfs_partition.h"

#include <lib/fpromise/result.h>
#include <zircon/hw/gpt.h>

#include <cstdint>
#include <iostream>

#include <safemath/checked_math.h>

#include "src/storage/fvm/format.h"
#include "src/storage/minfs/format.h"
#include "src/storage/minfs/fsck.h"
#include "src/storage/minfs/transaction_limits.h"
#include "src/storage/volume_image/address_descriptor.h"
#include "src/storage/volume_image/options.h"
#include "src/storage/volume_image/utils/block_utils.h"

namespace storage::volume_image {
namespace {

// Expected label for minfs volume
constexpr std::string_view kMinfsLabel = "data";

// Expected GUID for minfs instance.
constexpr std::array<uint8_t, kGuidLength> kMinfsTypeGuid = GUID_DATA_VALUE;

// For minfs we need to replace contents from the superblock, to make it look like its fvm based
// minfs.
class PatchedSuperblockReader final : public Reader {
 public:
  explicit PatchedSuperblockReader(std::unique_ptr<Reader> reader, uint64_t superblock_offset)
      : superblock_offset_(superblock_offset), reader_(std::move(reader)) {}

  uint64_t length() const final { return reader_->length(); }

  fpromise::result<void, std::string> Read(uint64_t offset,
                                           cpp20::span<uint8_t> buffer) const final {
    if (auto read_result = reader_->Read(offset, buffer); read_result.is_error()) {
      return read_result.take_error_result();
    }

    if (!(offset + buffer.size() > superblock_offset_) ||
        !(offset <= superblock_offset_ + minfs::kMinfsBlockSize)) {
      return fpromise::ok();
    }

    uint64_t bytes_before_superblock =
        superblock_offset_ > offset ? superblock_offset_ - offset : 0;
    uint64_t bytes_until_block_end =
        std::min(static_cast<uint64_t>(offset + buffer.size() - bytes_before_superblock),
                 superblock_offset_ + minfs::kMinfsBlockSize) -
        std::max(superblock_offset_, offset);
    memset(buffer.data() + bytes_before_superblock, 0, bytes_until_block_end);
    // now the superblock contents.
    if (offset <= superblock_offset_ + sizeof(minfs::Superblock)) {
      uint64_t bytes_since_superblock_offset =
          offset > superblock_offset_ ? offset - superblock_offset_ : 0;
      uint64_t bytes_remaining_from_superblock =
          sizeof(minfs::Superblock) - bytes_since_superblock_offset;
      uint64_t bytes_remaining_in_buffer =
          buffer.size() - bytes_before_superblock - bytes_since_superblock_offset;
      uint64_t bytes_to_read = std::min(bytes_remaining_in_buffer, bytes_remaining_from_superblock);
      memcpy(buffer.data() + bytes_before_superblock,
             reinterpret_cast<const uint8_t*>(&superblock_) + bytes_since_superblock_offset,
             bytes_to_read);
    }

    return fpromise::ok();
  };

  minfs::Superblock& superblock() { return superblock_; }

 private:
  minfs::Superblock superblock_;
  uint64_t superblock_offset_;
  std::unique_ptr<Reader> reader_;
};

}  // namespace

fpromise::result<Partition, std::string> CreateMinfsFvmPartition(
    std::unique_ptr<Reader> source_image, const PartitionOptions& partition_options,
    const FvmOptions& fvm_options) {
  if (fvm_options.slice_size % minfs::kMinfsBlockSize != 0) {
    return fpromise::error(
        "Fvm slice size must be a multiple of minfs block size. Expected minfs_block_size: " +
        std::to_string(minfs::kMinfsBlockSize) +
        " fvm_slice_size: " + std::to_string(fvm_options.slice_size) + ".");
  }

  // Load minfs superblock to obtain extent sizes and such.
  minfs::Superblock superblock = {};
  if (auto sb_read_result = source_image->Read(
          0,
          cpp20::span<uint8_t>(reinterpret_cast<uint8_t*>(&superblock), sizeof(minfs::Superblock)));
      sb_read_result.is_error()) {
    return sb_read_result.take_error_result();
  }

  // Minor validation that we are actually dealing with a minfs superblock.
  if (superblock.magic0 != minfs::kMinfsMagic0) {
    return fpromise::error(
        "Found bad magic0(" + std::to_string(superblock.magic0) +
        ") value in minfs superblock(Expected: " + std::to_string(minfs::kMinfsMagic0) + ").");
  }

  if (superblock.magic1 != minfs::kMinfsMagic1) {
    return fpromise::error(
        "Found bad magic1(" + std::to_string(superblock.magic1) +
        ") value in minfs superblock(Expected: " + std::to_string(minfs::kMinfsMagic1) + ").");
  }

  // Helper to calculate slice count.
  auto get_slice_count = [&fvm_options](const auto& mapping) {
    auto extent_size = std::max(mapping.count, mapping.size.value_or(0));
    return GetBlockCount(mapping.target, extent_size, fvm_options.slice_size);
  };

  VolumeDescriptor volume;
  volume.block_size = minfs::kMinfsBlockSize;
  volume.size = source_image->length();
  volume.encryption = EncryptionType::kZxcrypt;
  volume.name = kMinfsLabel;
  memcpy(volume.type.data(), kMinfsTypeGuid.data(), volume.type.size());
  memcpy(volume.instance.data(), fvm::kPlaceHolderInstanceGuid.data(), volume.instance.size());

  AddressDescriptor address;

  AddressMap superblock_mapping;
  superblock_mapping.source = 0;
  superblock_mapping.target = 0;
  superblock_mapping.count = sizeof(minfs::Superblock);
  superblock_mapping.options[EnumAsString(AddressMapOption::kFill)] = 0;

  AddressMap inode_bitmap_mapping;
  inode_bitmap_mapping.source = superblock.ibm_block * minfs::kMinfsBlockSize;
  inode_bitmap_mapping.target = minfs::kFVMBlockInodeBmStart * minfs::kMinfsBlockSize;
  inode_bitmap_mapping.count =
      (superblock.abm_block - superblock.ibm_block) * minfs::kMinfsBlockSize;
  inode_bitmap_mapping.size =
      std::max(inode_bitmap_mapping.count, static_cast<uint64_t>(minfs::BlocksRequiredForBits(
                                               partition_options.min_inode_count.value_or(0))) *
                                               minfs::kMinfsBlockSize);
  inode_bitmap_mapping.options[EnumAsString(AddressMapOption::kFill)] = 0;

  AddressMap data_bitmap_mapping;
  data_bitmap_mapping.source = superblock.abm_block * minfs::kMinfsBlockSize;
  data_bitmap_mapping.target = minfs::kFVMBlockDataBmStart * minfs::kMinfsBlockSize;
  data_bitmap_mapping.count =
      (superblock.ino_block - superblock.abm_block) * minfs::kMinfsBlockSize;
  data_bitmap_mapping.size =
      std::max(data_bitmap_mapping.count,
               static_cast<uint64_t>(minfs::BlocksRequiredForBits(GetBlockCount(
                   minfs::kFVMBlockDataBmStart * minfs::kMinfsBlockSize,
                   partition_options.min_data_bytes.value_or(0), minfs::kMinfsBlockSize))));
  data_bitmap_mapping.options[EnumAsString(AddressMapOption::kFill)] = 0;

  AddressMap inode_mapping;
  inode_mapping.source = superblock.ino_block * minfs::kMinfsBlockSize;
  inode_mapping.target = minfs::kFVMBlockInodeStart * minfs::kMinfsBlockSize;
  inode_mapping.count =
      (superblock.integrity_start_block - superblock.ino_block) * minfs::kMinfsBlockSize;
  inode_mapping.size =
      std::max(inode_mapping.count, static_cast<uint64_t>(minfs::BlocksRequiredForInode(
                                        partition_options.min_inode_count.value_or(0))) *
                                        minfs::kMinfsBlockSize);
  inode_mapping.options[EnumAsString(AddressMapOption::kFill)] = 0;

  AddressMap data_mapping;
  data_mapping.source = superblock.dat_block * minfs::kMinfsBlockSize;
  data_mapping.target = minfs::kFVMBlockDataStart * minfs::kMinfsBlockSize;
  data_mapping.count = superblock.block_count * minfs::kMinfsBlockSize;
  data_mapping.size =
      std::max(data_mapping.count,
               GetBlockCount(superblock.dat_block, partition_options.min_data_bytes.value_or(0),
                             minfs::kMinfsBlockSize) *
                   minfs::kMinfsBlockSize);

  AddressMap integrity_mapping;
  integrity_mapping.source = superblock.integrity_start_block * minfs::kMinfsBlockSize;
  integrity_mapping.target = minfs::kFvmSuperblockBackup * minfs::kMinfsBlockSize;
  integrity_mapping.count =
      (superblock.dat_block - superblock.integrity_start_block) * minfs::kMinfsBlockSize;

  auto patched_superblock_reader =
      std::make_unique<PatchedSuperblockReader>(std::move(source_image), 0);

  auto& patched_superblock = patched_superblock_reader->superblock();
  patched_superblock = superblock;

  patched_superblock.slice_size = fvm_options.slice_size;
  patched_superblock.flags |= minfs::kMinfsFlagFVM;

  patched_superblock.ibm_slices = get_slice_count(inode_bitmap_mapping);
  patched_superblock.abm_slices = get_slice_count(data_bitmap_mapping);
  patched_superblock.ino_slices = get_slice_count(inode_mapping);
  patched_superblock.dat_slices = get_slice_count(data_mapping);

  patched_superblock.inode_count = safemath::checked_cast<uint32_t>(
      get_slice_count(inode_mapping) * fvm_options.slice_size / minfs::kMinfsInodeSize);
  patched_superblock.block_count = safemath::checked_cast<uint32_t>(
      get_slice_count(data_mapping) * fvm_options.slice_size / minfs::kMinfsBlockSize);

  patched_superblock.ibm_block = minfs::kFVMBlockInodeBmStart;
  patched_superblock.abm_block = minfs::kFVMBlockDataBmStart;
  patched_superblock.ino_block = minfs::kFVMBlockInodeStart;
  patched_superblock.integrity_start_block = minfs::kFvmSuperblockBackup;
  patched_superblock.dat_block = minfs::kFVMBlockDataStart;

  // Calculate recommended journal slices based on the patched superblock.
  minfs::TransactionLimits limits(patched_superblock);

  integrity_mapping.size = std::max(
      integrity_mapping.count,
      static_cast<uint64_t>(limits.GetRecommendedIntegrityBlocks()) * minfs::kMinfsBlockSize);
  patched_superblock.integrity_slices = get_slice_count(integrity_mapping);

  minfs::UpdateChecksum(&patched_superblock);

  auto patched_superblock_and_backup_superblock_reader = std::make_unique<PatchedSuperblockReader>(
      std::move(patched_superblock_reader),
      superblock.integrity_start_block * minfs::kMinfsBlockSize);
  patched_superblock_and_backup_superblock_reader->superblock() = patched_superblock;

  address.mappings.push_back(superblock_mapping);
  address.mappings.push_back(inode_bitmap_mapping);
  address.mappings.push_back(data_bitmap_mapping);
  address.mappings.push_back(inode_mapping);
  address.mappings.push_back(integrity_mapping);
  address.mappings.push_back(data_mapping);

  uint64_t accumulated_slices = 0;
  for (const auto& mapping : address.mappings) {
    accumulated_slices += get_slice_count(mapping);
  }
  uint64_t accumulated_bytes = accumulated_slices * fvm_options.slice_size;

  if (partition_options.max_bytes.has_value() &&
      accumulated_bytes > partition_options.max_bytes.value()) {
    return fpromise::error("Minfs FVM Partition allocated " + std::to_string(accumulated_slices) +
                           "(" + std::to_string(accumulated_bytes) +
                           " bytes) exceeding provided upperbound |max_bytes|(" +
                           std::to_string(partition_options.max_bytes.value()) + ").");
  }

  return fpromise::ok(
      Partition(volume, address, std::move(patched_superblock_and_backup_superblock_reader)));
}

}  // namespace storage::volume_image
