// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/adapter/blobfs_partition.h"

#include <lib/fit/function.h>
#include <lib/fpromise/result.h>
#include <zircon/hw/gpt.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string_view>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/fvm/format.h"
#include "src/storage/volume_image/fvm/options.h"
#include "src/storage/volume_image/utils/block_utils.h"
#include "src/storage/volume_image/utils/fd_reader.h"
#include "src/storage/volume_image/utils/guid.h"
#include "src/storage/volume_image/utils/reader.h"

namespace storage::volume_image {
namespace {

constexpr std::string_view kBlobfsImagePath =
    STORAGE_VOLUME_IMAGE_ADAPTER_TEST_IMAGE_PATH "test_blobfs.blk";

std::array<uint8_t, kGuidLength> kBlobfsTypeGuid = GUID_BLOB_VALUE;
std::array<uint8_t, kGuidLength> kBlobfsInstanceGuid = fvm::kPlaceHolderInstanceGuid;

FvmOptions MakeFvmOptions(uint64_t slice_size) {
  FvmOptions options;
  options.slice_size = slice_size;
  return options;
}

constexpr uint64_t kSliceSize = 32u * (1u << 10);

class FakeReader final : public Reader {
 public:
  uint64_t length() const final { return 0; }

  fpromise::result<void, std::string> Read(uint64_t offset,
                                           cpp20::span<uint8_t> buffer) const final {
    memset(buffer.data(), 0, buffer.size());
    if (offset == 0) {
      memcpy(buffer.data(), &superblock_, sizeof(superblock_));
      return fpromise::ok();
    }
    return fpromise::ok();
  }

  blobfs::Superblock& superblock() { return superblock_; }

 private:
  blobfs::Superblock superblock_ = {};
};

TEST(BlobfsPartitionTest, BackupSuperblockDoesntFitInFirstSliceIsError) {
  auto fvm_options = MakeFvmOptions(blobfs::kBlobfsBlockSize);
  PartitionOptions partition_options;

  auto fake_reader = std::make_unique<FakeReader>();
  ASSERT_TRUE(
      CreateBlobfsFvmPartition(std::move(fake_reader), partition_options, fvm_options).is_error());
}

TEST(BlobfsPartitionTest, SliceSizeNotMultipleOfBlobfsBlockSizeIsError) {
  auto fvm_options = MakeFvmOptions(blobfs::kBlobfsBlockSize - 1);
  PartitionOptions partition_options;

  auto fake_reader = std::make_unique<FakeReader>();
  ASSERT_TRUE(
      CreateBlobfsFvmPartition(std::move(fake_reader), partition_options, fvm_options).is_error());
}

TEST(BlobfsPartitionTest, ImageWithBadMagicIsError) {
  auto fvm_options = MakeFvmOptions(kSliceSize);
  PartitionOptions partition_options;

  auto fake_reader = std::make_unique<FakeReader>();
  fake_reader->superblock().magic0 = blobfs::kBlobfsMagic0;
  fake_reader->superblock().magic1 = 1;
  ASSERT_TRUE(
      CreateBlobfsFvmPartition(std::move(fake_reader), partition_options, fvm_options).is_error());

  fake_reader = std::make_unique<FakeReader>();
  fake_reader->superblock().magic0 = 0;
  fake_reader->superblock().magic1 = blobfs::kBlobfsMagic1;
  ASSERT_TRUE(
      CreateBlobfsFvmPartition(std::move(fake_reader), partition_options, fvm_options).is_error());
}

std::optional<AddressMap> FindMappingStartingAt(uint64_t target_offset,
                                                const AddressDescriptor& address) {
  for (const auto& mapping : address.mappings) {
    if (mapping.target == target_offset) {
      return mapping;
    }
  }

  return std::nullopt;
}

void CheckSuperblock(const blobfs::Superblock& actual_superblock,
                     const blobfs::Superblock& original_superblock, const FvmOptions& fvm_options,
                     const PartitionOptions& partition_options) {
  // This should not be altered at all.
  EXPECT_EQ(actual_superblock.magic0, original_superblock.magic0);
  EXPECT_EQ(actual_superblock.magic1, original_superblock.magic1);
  EXPECT_EQ(actual_superblock.block_size, original_superblock.block_size);
  EXPECT_EQ(actual_superblock.alloc_block_count, original_superblock.alloc_block_count);
  EXPECT_EQ(actual_superblock.alloc_inode_count, original_superblock.alloc_inode_count);
  EXPECT_EQ(actual_superblock.major_version, original_superblock.major_version);
  EXPECT_EQ(actual_superblock.oldest_minor_version, original_superblock.oldest_minor_version);

  // The FVM flag MUST be set.
  EXPECT_TRUE((actual_superblock.flags & blobfs::kBlobFlagFVM) != 0);

  // This are updated as a result of slice allocation, and aligning blocks with slices.
  // At the very least contain enough for the original data.
  // Also check that partition paramters are honored.
  uint64_t min_data_blocks =
      GetBlockCount(blobfs::kFVMDataStart, partition_options.min_data_bytes.value_or(0),
                    blobfs::kBlobfsBlockSize);

  // Depending on slice alignment it might be a bit more
  EXPECT_GE(actual_superblock.inode_count, std::max(partition_options.min_inode_count.value_or(0),
                                                    original_superblock.inode_count));
  EXPECT_GE(actual_superblock.data_block_count,
            std::max(original_superblock.data_block_count, min_data_blocks));
  EXPECT_GE(actual_superblock.journal_block_count, original_superblock.journal_block_count);

  // This should match the result of the sb files above, but aligned to slices.
  EXPECT_EQ(actual_superblock.slice_size, fvm_options.slice_size);
  EXPECT_EQ(actual_superblock.ino_slices,
            GetBlockCount(blobfs::kFVMNodeMapStart * blobfs::kBlobfsBlockSize,
                          actual_superblock.inode_count * blobfs::kBlobfsInodeSize,
                          fvm_options.slice_size));
  EXPECT_EQ(actual_superblock.dat_slices,
            actual_superblock.data_block_count * blobfs::kBlobfsBlockSize / fvm_options.slice_size);
  EXPECT_EQ(
      actual_superblock.journal_slices,
      actual_superblock.journal_block_count * blobfs::kBlobfsBlockSize / fvm_options.slice_size);
  EXPECT_EQ(actual_superblock.abm_slices,
            GetBlockCount(blobfs::kFVMBlockMapStart,
                          blobfs::BlocksRequiredForBits(actual_superblock.data_block_count) *
                              blobfs::kBlobfsBlockSize,
                          fvm_options.slice_size));

  // Check if there are leftover bytes, they are assigned to the journal.
  if (partition_options.max_bytes.has_value()) {
    uint64_t max_slices_for_leftovers =
        GetBlockCount(0, partition_options.max_bytes.value(), fvm_options.slice_size);
    // slices for the journal, from original image.
    uint64_t min_journal_slices = GetBlockCount(
        blobfs::kFVMJournalStart,
        original_superblock.journal_block_count * blobfs::kBlobfsBlockSize, fvm_options.slice_size);
    uint64_t slices_before_leftovers = 1 + actual_superblock.abm_slices +
                                       actual_superblock.dat_slices + actual_superblock.ino_slices +
                                       min_journal_slices;
    if (max_slices_for_leftovers > slices_before_leftovers) {
      uint64_t delta = max_slices_for_leftovers - slices_before_leftovers;
      EXPECT_EQ(actual_superblock.journal_slices, delta + min_journal_slices);
    }
  }
}

void CheckNonSuperBlockMapping(const Partition& partition, const Reader& original_reader) {
  std::vector<uint8_t> original_mapping_contents;
  std::vector<uint8_t> mapping_contents;
  // Now check that the reader has the correct data for the rest of the mappings.
  for (uint64_t mapping_index = 1; mapping_index < partition.address().mappings.size();
       ++mapping_index) {
    const auto& mapping = partition.address().mappings[mapping_index];
    SCOPED_TRACE(testing::Message() << "Comparing mapping index " << mapping_index
                                    << " mapping: \n " << mapping.DebugString());
    // Only count bytes are backed by source data.
    mapping_contents.resize(mapping.count, 0);
    original_mapping_contents.resize(mapping.count, 0);

    // Adjust source for the extra block added for the backup superblock.
    auto original_read_result =
        original_reader.Read(mapping.source - blobfs::kBlobfsBlockSize, original_mapping_contents);
    ASSERT_TRUE(original_read_result.is_ok()) << original_read_result.error();

    auto read_result = partition.reader()->Read(mapping.source, mapping_contents);
    ASSERT_TRUE(read_result.is_ok()) << read_result.error();

    EXPECT_TRUE(memcmp(mapping_contents.data(), original_mapping_contents.data(),
                       mapping_contents.size()) == 0);
  }
}

void CheckJournalMapping(const Partition& partition, const Reader& original_reader,
                         const blobfs::Superblock& original_superblock) {
  std::vector<uint8_t> original_mapping_contents;
  std::vector<uint8_t> mapping_contents;
  auto mapping_or = FindMappingStartingAt(blobfs::kFVMJournalStart * blobfs::kBlobfsBlockSize,
                                          partition.address());
  ASSERT_TRUE(mapping_or.has_value());
  auto mapping = mapping_or.value();

  // Only count bytes are backed by source data.
  mapping_contents.resize(mapping.count, 0);
  original_mapping_contents.resize(mapping.count, 0);

  // Adjust source for the extra block added for the backup superblock.
  auto original_read_result = original_reader.Read(
      blobfs::JournalStartBlock(original_superblock) * blobfs::kBlobfsBlockSize,
      original_mapping_contents);

  ASSERT_TRUE(original_read_result.is_ok()) << original_read_result.error();

  auto read_result = partition.reader()->Read(mapping.source, mapping_contents);
  ASSERT_TRUE(read_result.is_ok()) << read_result.error();

  EXPECT_TRUE(memcmp(mapping_contents.data(), original_mapping_contents.data(),
                     mapping_contents.size()) == 0);
}

void CheckPartition(const Partition& partition) {
  EXPECT_EQ(partition.volume().name, "blobfs");
  EXPECT_THAT(partition.volume().instance, testing::ElementsAreArray(kBlobfsInstanceGuid));
  EXPECT_THAT(partition.volume().type, testing::ElementsAreArray(kBlobfsTypeGuid));

  auto superblock_mapping = FindMappingStartingAt(0, partition.address());
  ASSERT_TRUE(superblock_mapping.has_value());
  EXPECT_EQ(superblock_mapping->source, 0u);
  EXPECT_EQ(superblock_mapping->count, 2 * blobfs::kBlobfsBlockSize);
  EXPECT_THAT(superblock_mapping->options,
              testing::Contains(testing::Pair(EnumAsString(AddressMapOption::kFill), 0u)));

  // 5 total different regions, including superblock region.
  ASSERT_EQ(partition.address().mappings.size(), 5u);

  // Check that mappings for all the regions exist.
  auto inode_mapping = FindMappingStartingAt(blobfs::kFVMNodeMapStart * blobfs::kBlobfsBlockSize,
                                             partition.address());
  ASSERT_TRUE(inode_mapping.has_value());
  EXPECT_THAT(inode_mapping->options,
              testing::Contains(testing::Pair(EnumAsString(AddressMapOption::kFill), 0u)));

  auto bitmap_mapping = FindMappingStartingAt(blobfs::kFVMBlockMapStart * blobfs::kBlobfsBlockSize,
                                              partition.address());
  ASSERT_TRUE(bitmap_mapping.has_value());
  EXPECT_THAT(bitmap_mapping->options,
              testing::Contains(testing::Pair(EnumAsString(AddressMapOption::kFill), 0u)));

  // This two just need to exist, they should not be zeroed like the above.
  ASSERT_TRUE(
      FindMappingStartingAt(blobfs::kFVMDataStart * blobfs::kBlobfsBlockSize, partition.address())
          .has_value());
  ASSERT_TRUE(FindMappingStartingAt(blobfs::kFVMJournalStart * blobfs::kBlobfsBlockSize,
                                    partition.address())
                  .has_value());
}

struct SuperBlocks {
  std::vector<uint8_t> original_superblock;
  std::vector<uint8_t> actual_superblock;
};

std::optional<SuperBlocks> ReadSuperblocks(const Reader& original_blobfs,
                                           const Reader& blobfs_with_backup) {
  std::vector<uint8_t> original_superblock;
  original_superblock.resize(blobfs::kBlobfsBlockSize, 0);
  auto original_superblock_result = original_blobfs.Read(0, original_superblock);
  EXPECT_TRUE(original_superblock_result.is_ok()) << original_superblock_result.error();

  std::vector<uint8_t> superblock;
  superblock.resize(blobfs::kBlobfsBlockSize, 0);
  auto superblock_result = blobfs_with_backup.Read(0, superblock);
  EXPECT_TRUE(superblock_result.is_ok()) << superblock_result.error();

  std::vector<uint8_t> backup_superblock;
  backup_superblock.resize(blobfs::kBlobfsBlockSize, 0);
  auto backup_superblock_result =
      blobfs_with_backup.Read(blobfs::kBlobfsBlockSize, backup_superblock);
  EXPECT_TRUE(backup_superblock_result.is_ok()) << backup_superblock_result.error();

  if (testing::Test::HasFailure()) {
    return std::nullopt;
  }

  // Check that superblock and backupsuperblock are equal.
  EXPECT_TRUE(memcmp(backup_superblock.data(), superblock.data(), backup_superblock.size()) == 0);

  // Check that reading both superblocks together has the same result.
  std::vector<uint8_t> both_superblocks;
  both_superblocks.resize(2 * blobfs::kBlobfsBlockSize, 0);
  auto both_superblocks_result = blobfs_with_backup.Read(0, both_superblocks);
  EXPECT_TRUE(both_superblocks_result.is_ok()) << both_superblocks_result.error();
  if (testing::Test::HasFailure()) {
    return std::nullopt;
  }
  EXPECT_TRUE(memcmp(both_superblocks.data(), both_superblocks.data() + blobfs::kBlobfsBlockSize,
                     blobfs::kBlobfsBlockSize) == 0);

  if (testing::Test::HasFailure()) {
    return std::nullopt;
  }

  return SuperBlocks{original_superblock, superblock};
}

TEST(BlobfsPartitionTest, PartitionDataAndReaderIsCorrect) {
  auto fvm_options = MakeFvmOptions(kSliceSize);
  PartitionOptions partition_options;

  auto original_blobfs_reader_or = FdReader::Create(kBlobfsImagePath);
  ASSERT_TRUE(original_blobfs_reader_or.is_ok()) << original_blobfs_reader_or.error();
  auto original_blobfs_reader = original_blobfs_reader_or.take_value();

  auto blobfs_reader_or = FdReader::Create(kBlobfsImagePath);
  ASSERT_TRUE(blobfs_reader_or.is_ok()) << blobfs_reader_or.error();
  std::unique_ptr<Reader> blobfs_reader = std::make_unique<FdReader>(blobfs_reader_or.take_value());

  auto partition_or =
      CreateBlobfsFvmPartition(std::move(blobfs_reader), partition_options, fvm_options);
  ASSERT_TRUE(partition_or.is_ok()) << partition_or.error();
  auto partition = partition_or.take_value();

  ASSERT_NO_FATAL_FAILURE(CheckPartition(partition));

  auto maybe_superblocks = ReadSuperblocks(original_blobfs_reader, *partition.reader());
  ASSERT_TRUE(maybe_superblocks.has_value());
  auto [original_superblock, superblock] = maybe_superblocks.value();

  // Check the fields in the superblock are at least as big as those in the original image, and
  // the allocation counts remain unchanged.
  const blobfs::Superblock* sb = reinterpret_cast<blobfs::Superblock*>(superblock.data());
  const blobfs::Superblock* original_sb =
      reinterpret_cast<blobfs::Superblock*>(original_superblock.data());

  ASSERT_NO_FATAL_FAILURE(CheckSuperblock(*sb, *original_sb, fvm_options, partition_options));
  ASSERT_NO_FATAL_FAILURE(CheckNonSuperBlockMapping(partition, original_blobfs_reader));
}

TEST(BlobfsPartitionTest, PartitionDataAndReaderIsCorrectWithMinimumInodeCountHigherThanImage) {
  auto fvm_options = MakeFvmOptions(kSliceSize);
  PartitionOptions partition_options;

  auto original_blobfs_reader_or = FdReader::Create(kBlobfsImagePath);
  ASSERT_TRUE(original_blobfs_reader_or.is_ok()) << original_blobfs_reader_or.error();
  auto original_blobfs_reader = original_blobfs_reader_or.take_value();

  blobfs::Superblock sb_tmp = {};
  auto read_result = original_blobfs_reader.Read(
      0, cpp20::span<uint8_t>(reinterpret_cast<uint8_t*>(&sb_tmp), sizeof(sb_tmp)));
  ASSERT_TRUE(read_result.is_ok()) << read_result.error();

  // Add as many inodes such that at least an extra slice is allocated.
  partition_options.min_inode_count =
      sb_tmp.inode_count + GetBlockCount(0, fvm_options.slice_size, blobfs::kBlobfsInodeSize);

  auto blobfs_reader_or = FdReader::Create(kBlobfsImagePath);
  ASSERT_TRUE(blobfs_reader_or.is_ok()) << blobfs_reader_or.error();
  std::unique_ptr<Reader> blobfs_reader = std::make_unique<FdReader>(blobfs_reader_or.take_value());

  auto partition_or =
      CreateBlobfsFvmPartition(std::move(blobfs_reader), partition_options, fvm_options);
  ASSERT_TRUE(partition_or.is_ok()) << partition_or.error();
  auto partition = partition_or.take_value();

  ASSERT_NO_FATAL_FAILURE(CheckPartition(partition));

  auto maybe_superblocks = ReadSuperblocks(original_blobfs_reader, *partition.reader());
  ASSERT_TRUE(maybe_superblocks.has_value());
  auto [original_superblock, superblock] = maybe_superblocks.value();

  // Check the fields in the superblock are at least as big as those in the original image, and
  // the allocation counts remain unchanged.
  const blobfs::Superblock* sb = reinterpret_cast<blobfs::Superblock*>(superblock.data());
  const blobfs::Superblock* original_sb =
      reinterpret_cast<blobfs::Superblock*>(original_superblock.data());

  // Adjust the expected inode count such that it matches the amount of slices that would be
  // required for the requested options.
  auto expected_inode_count =
      GetBlockCount(blobfs::kFVMNodeMapStart,
                    blobfs::BlocksRequiredForInode(partition_options.min_inode_count.value()) *
                        blobfs::kBlobfsBlockSize,
                    blobfs::kBlobfsInodeSize);
  ASSERT_EQ(sb->inode_count, expected_inode_count);
  ASSERT_NO_FATAL_FAILURE(CheckSuperblock(*sb, *original_sb, fvm_options, partition_options));
  ASSERT_NO_FATAL_FAILURE(CheckNonSuperBlockMapping(partition, original_blobfs_reader));
  ASSERT_NO_FATAL_FAILURE(CheckJournalMapping(partition, original_blobfs_reader, *original_sb));
}

TEST(BlobfsPartitionTest, PartitionDataAndReaderIsCorrectWithMinimumInodeCountLowerThanImage) {
  auto fvm_options = MakeFvmOptions(kSliceSize);
  PartitionOptions partition_options;
  partition_options.min_inode_count = 0;

  auto original_blobfs_reader_or = FdReader::Create(kBlobfsImagePath);
  ASSERT_TRUE(original_blobfs_reader_or.is_ok()) << original_blobfs_reader_or.error();
  auto original_blobfs_reader = original_blobfs_reader_or.take_value();

  auto blobfs_reader_or = FdReader::Create(kBlobfsImagePath);
  ASSERT_TRUE(blobfs_reader_or.is_ok()) << blobfs_reader_or.error();
  std::unique_ptr<Reader> blobfs_reader = std::make_unique<FdReader>(blobfs_reader_or.take_value());

  auto partition_or =
      CreateBlobfsFvmPartition(std::move(blobfs_reader), partition_options, fvm_options);
  ASSERT_TRUE(partition_or.is_ok()) << partition_or.error();
  auto partition = partition_or.take_value();

  ASSERT_NO_FATAL_FAILURE(CheckPartition(partition));

  auto maybe_superblocks = ReadSuperblocks(original_blobfs_reader, *partition.reader());
  ASSERT_TRUE(maybe_superblocks.has_value());
  auto [original_superblock, superblock] = maybe_superblocks.value();

  // Check the fields in the superblock are at least as big as those in the original image, and
  // the allocation counts remain unchanged.
  const blobfs::Superblock* sb = reinterpret_cast<blobfs::Superblock*>(superblock.data());
  const blobfs::Superblock* original_sb =
      reinterpret_cast<blobfs::Superblock*>(original_superblock.data());

  ASSERT_NE(sb->inode_count, 0u);
  ASSERT_NO_FATAL_FAILURE(CheckSuperblock(*sb, *original_sb, fvm_options, partition_options));
  ASSERT_NO_FATAL_FAILURE(CheckNonSuperBlockMapping(partition, original_blobfs_reader));
  ASSERT_NO_FATAL_FAILURE(CheckJournalMapping(partition, original_blobfs_reader, *original_sb));
}

TEST(BlobfsPartitionTest, PartitionDataAndReaderIsCorrectWithMinimumDataBytesHigherThanImage) {
  auto fvm_options = MakeFvmOptions(kSliceSize);
  PartitionOptions partition_options;

  auto original_blobfs_reader_or = FdReader::Create(kBlobfsImagePath);
  ASSERT_TRUE(original_blobfs_reader_or.is_ok()) << original_blobfs_reader_or.error();
  auto original_blobfs_reader = original_blobfs_reader_or.take_value();

  blobfs::Superblock sb_tmp = {};
  auto read_result = original_blobfs_reader.Read(
      0, cpp20::span<uint8_t>(reinterpret_cast<uint8_t*>(&sb_tmp), sizeof(sb_tmp)));
  ASSERT_TRUE(read_result.is_ok()) << read_result.error();

  // Add an extra slice worth of blocks.
  partition_options.min_data_bytes =
      (sb_tmp.data_block_count +
       GetBlockCount(0, fvm_options.slice_size, blobfs::kBlobfsBlockSize)) *
      blobfs::kBlobfsBlockSize;

  auto blobfs_reader_or = FdReader::Create(kBlobfsImagePath);
  ASSERT_TRUE(blobfs_reader_or.is_ok()) << blobfs_reader_or.error();
  std::unique_ptr<Reader> blobfs_reader = std::make_unique<FdReader>(blobfs_reader_or.take_value());

  auto partition_or =
      CreateBlobfsFvmPartition(std::move(blobfs_reader), partition_options, fvm_options);
  ASSERT_TRUE(partition_or.is_ok()) << partition_or.error();
  auto partition = partition_or.take_value();

  ASSERT_NO_FATAL_FAILURE(CheckPartition(partition));

  auto maybe_superblocks = ReadSuperblocks(original_blobfs_reader, *partition.reader());
  ASSERT_TRUE(maybe_superblocks.has_value());
  auto [original_superblock, superblock] = maybe_superblocks.value();

  // Check the fields in the superblock are at least as big as those in the original image, and
  // the allocation counts remain unchanged.
  const blobfs::Superblock* sb = reinterpret_cast<blobfs::Superblock*>(superblock.data());
  const blobfs::Superblock* original_sb =
      reinterpret_cast<blobfs::Superblock*>(original_superblock.data());

  ASSERT_EQ(sb->data_block_count,
            GetBlockCount(blobfs::kFVMDataStart, partition_options.min_data_bytes.value(),
                          blobfs::kBlobfsBlockSize));
  ASSERT_NO_FATAL_FAILURE(CheckSuperblock(*sb, *original_sb, fvm_options, partition_options));
  ASSERT_NO_FATAL_FAILURE(CheckNonSuperBlockMapping(partition, original_blobfs_reader));
  ASSERT_NO_FATAL_FAILURE(CheckJournalMapping(partition, original_blobfs_reader, *original_sb));
}

TEST(BlobfsPartitionTest, PartitionDataAndReaderIsCorrectWithMinimumDataBytessLowerThanImage) {
  auto fvm_options = MakeFvmOptions(kSliceSize);
  PartitionOptions partition_options;

  auto original_blobfs_reader_or = FdReader::Create(kBlobfsImagePath);
  ASSERT_TRUE(original_blobfs_reader_or.is_ok()) << original_blobfs_reader_or.error();
  auto original_blobfs_reader = original_blobfs_reader_or.take_value();

  // Add an extra slice worth of blocks.
  partition_options.min_data_bytes = 0;

  auto blobfs_reader_or = FdReader::Create(kBlobfsImagePath);
  ASSERT_TRUE(blobfs_reader_or.is_ok()) << blobfs_reader_or.error();
  std::unique_ptr<Reader> blobfs_reader = std::make_unique<FdReader>(blobfs_reader_or.take_value());

  auto partition_or =
      CreateBlobfsFvmPartition(std::move(blobfs_reader), partition_options, fvm_options);
  ASSERT_TRUE(partition_or.is_ok()) << partition_or.error();
  auto partition = partition_or.take_value();
  ;
  ASSERT_NO_FATAL_FAILURE(CheckPartition(partition));

  auto maybe_superblocks = ReadSuperblocks(original_blobfs_reader, *partition.reader());
  ASSERT_TRUE(maybe_superblocks.has_value());
  auto [original_superblock, superblock] = maybe_superblocks.value();

  // Check the fields in the superblock are at least as big as those in the original image, and
  // the allocation counts remain unchanged.
  const blobfs::Superblock* sb = reinterpret_cast<blobfs::Superblock*>(superblock.data());
  const blobfs::Superblock* original_sb =
      reinterpret_cast<blobfs::Superblock*>(original_superblock.data());

  ASSERT_NE(sb->data_block_count, 0u);
  ASSERT_NO_FATAL_FAILURE(CheckSuperblock(*sb, *original_sb, fvm_options, partition_options));
  ASSERT_NO_FATAL_FAILURE(CheckNonSuperBlockMapping(partition, original_blobfs_reader));
  ASSERT_NO_FATAL_FAILURE(CheckJournalMapping(partition, original_blobfs_reader, *original_sb));
}

TEST(BlobfsPartitionTest,
     PartitionDataAndReaderIsCorrectWithMaxAllocatedBytesForLeftOverHigherThanImage) {
  auto fvm_options = MakeFvmOptions(kSliceSize);
  PartitionOptions partition_options;

  auto original_blobfs_reader_or = FdReader::Create(kBlobfsImagePath);
  ASSERT_TRUE(original_blobfs_reader_or.is_ok()) << original_blobfs_reader_or.error();
  auto original_blobfs_reader = original_blobfs_reader_or.take_value();
  // Set it to an absurd amount, this should only be reflected on journal slices.
  partition_options.max_bytes = 10u * (1u << 30);

  auto blobfs_reader_or = FdReader::Create(kBlobfsImagePath);
  ASSERT_TRUE(blobfs_reader_or.is_ok()) << blobfs_reader_or.error();
  std::unique_ptr<Reader> blobfs_reader = std::make_unique<FdReader>(blobfs_reader_or.take_value());

  auto partition_or =
      CreateBlobfsFvmPartition(std::move(blobfs_reader), partition_options, fvm_options);
  ASSERT_TRUE(partition_or.is_ok()) << partition_or.error();
  auto partition = partition_or.take_value();
  ASSERT_NO_FATAL_FAILURE(CheckPartition(partition));

  auto maybe_superblocks = ReadSuperblocks(original_blobfs_reader, *partition.reader());
  ASSERT_TRUE(maybe_superblocks.has_value());
  auto [original_superblock, superblock] = maybe_superblocks.value();

  // Check the fields in the superblock are at least as big as those in the original image, and
  // the allocation counts remain unchanged.
  const blobfs::Superblock* sb = reinterpret_cast<blobfs::Superblock*>(superblock.data());
  const blobfs::Superblock* original_sb =
      reinterpret_cast<blobfs::Superblock*>(original_superblock.data());

  // The actual value will be matched in the |CheckSuperblock|, but we can at least verify, that
  // is bigger than would have been for the original journal blocks.
  uint64_t old_blocks =
      GetBlockCount(blobfs::kFVMJournalStart,
                    GetBlockCount(0, original_sb->journal_block_count * blobfs::kBlobfsBlockSize,
                                  fvm_options.slice_size) *
                        fvm_options.slice_size,
                    blobfs::kBlobfsBlockSize);
  ASSERT_GT(sb->journal_block_count, old_blocks);
  ASSERT_NO_FATAL_FAILURE(CheckSuperblock(*sb, *original_sb, fvm_options, partition_options));
  ASSERT_NO_FATAL_FAILURE(CheckNonSuperBlockMapping(partition, original_blobfs_reader));
  ASSERT_NO_FATAL_FAILURE(CheckJournalMapping(partition, original_blobfs_reader, *original_sb));
}

TEST(BlobfsPartitionTest, ExceedingMaxBytesIsError) {
  auto fvm_options = MakeFvmOptions(kSliceSize);
  PartitionOptions partition_options;

  auto original_blobfs_reader_or = FdReader::Create(kBlobfsImagePath);
  ASSERT_TRUE(original_blobfs_reader_or.is_ok()) << original_blobfs_reader_or.error();
  auto original_blobfs_reader = original_blobfs_reader_or.take_value();

  // Number of mappings - 1 slices, so this will be 4.
  partition_options.max_bytes = 4 * fvm_options.slice_size;

  auto blobfs_reader_or = FdReader::Create(kBlobfsImagePath);
  ASSERT_TRUE(blobfs_reader_or.is_ok()) << blobfs_reader_or.error();
  std::unique_ptr<Reader> blobfs_reader = std::make_unique<FdReader>(blobfs_reader_or.take_value());

  auto partition_or =
      CreateBlobfsFvmPartition(std::move(blobfs_reader), partition_options, fvm_options);
  ASSERT_TRUE(partition_or.is_error());
}

}  // namespace
}  // namespace storage::volume_image
