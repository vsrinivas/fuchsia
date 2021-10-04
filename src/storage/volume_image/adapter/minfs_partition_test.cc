// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/adapter/minfs_partition.h"

#include <zircon/hw/gpt.h>

#include <limits>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/storage/fvm/format.h"
#include "src/storage/minfs/format.h"
#include "src/storage/minfs/fsck.h"
#include "src/storage/minfs/transaction_limits.h"
#include "src/storage/volume_image/fvm/options.h"
#include "src/storage/volume_image/utils/block_utils.h"
#include "src/storage/volume_image/utils/fd_reader.h"
#include "src/storage/volume_image/utils/guid.h"
#include "src/storage/volume_image/utils/reader.h"

namespace storage::volume_image {
namespace {

constexpr std::string_view kMinfsImagePath =
    STORAGE_VOLUME_IMAGE_ADAPTER_TEST_IMAGE_PATH "test_minfs.blk";

std::array<uint8_t, kGuidLength> kMinfsTypeGuid = GUID_DATA_VALUE;
std::array<uint8_t, kGuidLength> kMinfsInstanceGuid = fvm::kPlaceHolderInstanceGuid;

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
      memcpy(buffer.data(), &superblock_, std::min(sizeof(superblock_), buffer.size()));
    }
    return fpromise::ok();
  }

  minfs::Superblock& superblock() { return superblock_; }

 private:
  minfs::Superblock superblock_ = {};
};

TEST(MinfsPartitionTest, SliceSizeNotMultipleOfMinfsBlockSizeIsError) {
  auto fvm_options = MakeFvmOptions(minfs::kMinfsBlockSize - 1);
  PartitionOptions partition_options;

  auto fake_reader = std::make_unique<FakeReader>();
  ASSERT_TRUE(
      CreateMinfsFvmPartition(std::move(fake_reader), partition_options, fvm_options).is_error());
}

TEST(MinfsPartitionTest, ImageWithBadMagicIsError) {
  auto fvm_options = MakeFvmOptions(kSliceSize);
  PartitionOptions partition_options;

  auto fake_reader = std::make_unique<FakeReader>();
  fake_reader->superblock().magic0 = minfs::kMinfsMagic0;
  fake_reader->superblock().magic1 = 1;
  ASSERT_TRUE(
      CreateMinfsFvmPartition(std::move(fake_reader), partition_options, fvm_options).is_error());

  fake_reader = std::make_unique<FakeReader>();
  fake_reader->superblock().magic0 = 0;
  fake_reader->superblock().magic1 = minfs::kMinfsMagic1;
  ASSERT_TRUE(
      CreateMinfsFvmPartition(std::move(fake_reader), partition_options, fvm_options).is_error());
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

void CheckPartition(const Partition& partition, const minfs::Superblock& original_superblock) {
  EXPECT_EQ(partition.volume().name, "data");
  EXPECT_THAT(partition.volume().instance, testing::ElementsAreArray(kMinfsInstanceGuid));
  EXPECT_THAT(partition.volume().type, testing::ElementsAreArray(kMinfsTypeGuid));

  // 6 total different regions, including superblock region.
  ASSERT_EQ(partition.address().mappings.size(), 6u);

  auto superblock_mapping = FindMappingStartingAt(0, partition.address());
  ASSERT_TRUE(superblock_mapping.has_value());
  EXPECT_EQ(superblock_mapping->source, 0u);
  EXPECT_EQ(superblock_mapping->count, sizeof(minfs::Superblock));
  EXPECT_THAT(superblock_mapping->options,
              testing::Contains(testing::Pair(EnumAsString(AddressMapOption::kFill), 0u)));

  auto inode_bitmap_mapping = FindMappingStartingAt(
      minfs::kFVMBlockInodeBmStart * minfs::kMinfsBlockSize, partition.address());
  ASSERT_TRUE(inode_bitmap_mapping.has_value());
  EXPECT_EQ(inode_bitmap_mapping->source, original_superblock.ibm_block * minfs::kMinfsBlockSize);
  EXPECT_THAT(inode_bitmap_mapping->options,
              testing::Contains(testing::Pair(EnumAsString(AddressMapOption::kFill), 0u)));

  auto data_bitmap_mapping = FindMappingStartingAt(
      minfs::kFVMBlockDataBmStart * minfs::kMinfsBlockSize, partition.address());
  ASSERT_TRUE(data_bitmap_mapping.has_value());
  EXPECT_EQ(data_bitmap_mapping->source, original_superblock.abm_block * minfs::kMinfsBlockSize);
  EXPECT_THAT(data_bitmap_mapping->options,
              testing::Contains(testing::Pair(EnumAsString(AddressMapOption::kFill), 0u)));

  auto inode_mapping = FindMappingStartingAt(minfs::kFVMBlockInodeStart * minfs::kMinfsBlockSize,
                                             partition.address());
  ASSERT_TRUE(inode_mapping.has_value());
  EXPECT_EQ(inode_mapping->source, original_superblock.ino_block * minfs::kMinfsBlockSize);
  EXPECT_THAT(inode_mapping->options,
              testing::Contains(testing::Pair(EnumAsString(AddressMapOption::kFill), 0u)));

  auto integrity_mapping = FindMappingStartingAt(
      minfs::kFvmSuperblockBackup * minfs::kMinfsBlockSize, partition.address());
  ASSERT_TRUE(integrity_mapping.has_value());
  EXPECT_EQ(integrity_mapping->source,
            original_superblock.integrity_start_block * minfs::kMinfsBlockSize);

  auto data_mapping = FindMappingStartingAt(minfs::kFVMBlockDataStart * minfs::kMinfsBlockSize,
                                            partition.address());
  ASSERT_TRUE(data_mapping.has_value());
  EXPECT_EQ(data_mapping->source, original_superblock.dat_block * minfs::kMinfsBlockSize);
}

struct SuperBlocks {
  std::vector<uint8_t> original_superblock;
  std::vector<uint8_t> actual_superblock;
};

std::optional<SuperBlocks> ReadSuperblocks(const Partition& partition, const Reader& source_image) {
  std::vector<uint8_t> original_superblock;
  original_superblock.resize(minfs::kMinfsBlockSize, 0);
  auto original_superblock_result = source_image.Read(0, original_superblock);
  EXPECT_TRUE(original_superblock_result.is_ok()) << original_superblock_result.error();

  std::vector<uint8_t> superblock;
  superblock.resize(minfs::kMinfsBlockSize, 0);
  auto superblock_result = partition.reader()->Read(0, superblock);
  EXPECT_TRUE(superblock_result.is_ok()) << superblock_result.error();

  auto integrity_mapping = FindMappingStartingAt(
      minfs::kFvmSuperblockBackup * minfs::kMinfsBlockSize, partition.address());
  EXPECT_TRUE(integrity_mapping.has_value());
  if (testing::Test::HasFailure()) {
    return std::nullopt;
  }

  std::vector<uint8_t> backup_superblock;
  backup_superblock.resize(minfs::kMinfsBlockSize, 0);
  auto backup_superblock_result =
      partition.reader()->Read(integrity_mapping->source, backup_superblock);
  EXPECT_TRUE(backup_superblock_result.is_ok()) << backup_superblock_result.error();

  if (testing::Test::HasFailure()) {
    return std::nullopt;
  }

  EXPECT_TRUE(memcmp(superblock.data(), backup_superblock.data(), backup_superblock.size()) == 0);

  if (testing::Test::HasFailure()) {
    return std::nullopt;
  }

  return SuperBlocks{original_superblock, superblock};
}

void CheckSuperblock(const minfs::Superblock& actual_superblock,
                     const minfs::Superblock& original_superblock, const FvmOptions& fvm_options,
                     const PartitionOptions& partition_options) {
  // This should not be altered at all.
  EXPECT_EQ(actual_superblock.magic0, original_superblock.magic0);
  EXPECT_EQ(actual_superblock.magic1, original_superblock.magic1);
  EXPECT_EQ(actual_superblock.block_size, original_superblock.block_size);
  EXPECT_EQ(actual_superblock.alloc_block_count, original_superblock.alloc_block_count);
  EXPECT_EQ(actual_superblock.alloc_inode_count, original_superblock.alloc_inode_count);
  EXPECT_EQ(actual_superblock.major_version, original_superblock.major_version);
  EXPECT_EQ(actual_superblock.inode_size, original_superblock.inode_size);
  EXPECT_EQ(actual_superblock.oldest_minor_version, original_superblock.oldest_minor_version);
  EXPECT_EQ(actual_superblock.unlinked_head, original_superblock.unlinked_head);
  EXPECT_EQ(actual_superblock.unlinked_tail, original_superblock.unlinked_tail);

  // The FVM flag MUST be set.
  EXPECT_TRUE((actual_superblock.flags & minfs::kMinfsFlagFVM) != 0);
  EXPECT_EQ(actual_superblock.slice_size, fvm_options.slice_size);

  // This are updated as a result of slice allocation, and aligning blocks with slices.
  // At the very least contain enough for the original data.
  // Also check that partition parameters are honored.
  uint64_t inode_count = static_cast<uint64_t>(original_superblock.inode_count);
  uint64_t inode_blocks = original_superblock.integrity_start_block - original_superblock.ino_block;
  uint64_t inode_bitmap_blocks = original_superblock.abm_block - original_superblock.ibm_block;
  if (inode_count < partition_options.min_inode_count.value_or(0)) {
    inode_blocks = minfs::BlocksRequiredForInode(partition_options.min_inode_count.value());
  }

  inode_bitmap_blocks = std::max(static_cast<uint64_t>(minfs::BlocksRequiredForBits(inode_count)),
                                 inode_bitmap_blocks);

  uint64_t data_blocks =
      GetBlockCount(minfs::kFVMBlockDataStart, partition_options.min_data_bytes.value_or(0),
                    minfs::kMinfsBlockSize);
  if (data_blocks < original_superblock.block_count) {
    data_blocks = original_superblock.block_count;
  }

  uint64_t data_bitmap_blocks =
      std::max(original_superblock.ino_block - original_superblock.abm_block,
               minfs::BlocksRequiredForBits(data_blocks));

  uint64_t integrity_blocks =
      original_superblock.dat_block - original_superblock.integrity_start_block;
  minfs::TransactionLimits limits(original_superblock);
  integrity_blocks =
      std::max(integrity_blocks, static_cast<uint64_t>(limits.GetRecommendedIntegrityBlocks()));

  auto get_slice_count = [&fvm_options](uint64_t block_count) {
    return GetBlockCount(0, block_count * minfs::kMinfsBlockSize, fvm_options.slice_size);
  };
  auto get_slice_bytes = [&fvm_options, &get_slice_count](uint64_t block_count) {
    return get_slice_count(block_count) * fvm_options.slice_size;
  };

  EXPECT_EQ(actual_superblock.inode_count, get_slice_bytes(inode_blocks) / minfs::kMinfsInodeSize);
  EXPECT_EQ(actual_superblock.block_count, get_slice_bytes(data_blocks) / minfs::kMinfsBlockSize);
  EXPECT_EQ(actual_superblock.ibm_block, minfs::kFVMBlockInodeBmStart);
  EXPECT_EQ(actual_superblock.abm_block, minfs::kFVMBlockDataBmStart);
  EXPECT_EQ(actual_superblock.ino_block, minfs::kFVMBlockInodeStart);
  EXPECT_EQ(actual_superblock.integrity_start_block, minfs::kFvmSuperblockBackup);
  EXPECT_EQ(actual_superblock.integrity_start_block, minfs::kFvmSuperblockBackup);

  // Now slices allocated for each extent should be correct as well.
  EXPECT_EQ(actual_superblock.ino_slices, get_slice_count(inode_blocks));
  EXPECT_EQ(actual_superblock.dat_slices, get_slice_count(data_blocks));
  EXPECT_EQ(actual_superblock.ibm_slices, get_slice_count(inode_bitmap_blocks));
  EXPECT_EQ(actual_superblock.abm_slices, get_slice_count(data_bitmap_blocks));
  EXPECT_EQ(actual_superblock.integrity_slices, get_slice_count(integrity_blocks));
}

void CheckNoNSuperBlockMappingContents(const Partition& partition, const Reader& original_reader) {
  std::vector<uint8_t> original_mapping_contents;
  std::vector<uint8_t> mapping_contents;
  // Now check that the reader has the correct data for the rest of the mappings.
  for (uint64_t mapping_index = 1; mapping_index < partition.address().mappings.size();
       ++mapping_index) {
    const auto& mapping = partition.address().mappings[mapping_index];
    SCOPED_TRACE(testing::Message() << "Comparing mapping index " << mapping_index
                                    << " mapping: \n " << mapping.DebugString());

    // Skip checking the backup superblock.
    if (mapping.target == minfs::kFvmSuperblockBackup * minfs::kMinfsBlockSize) {
      continue;
    }

    // Only count bytes are backed by source data.
    mapping_contents.resize(mapping.count, 0);
    original_mapping_contents.resize(mapping.count, 0);

    // Adjust source for the extra block added for the backup superblock.
    auto original_read_result = original_reader.Read(mapping.source, original_mapping_contents);
    ASSERT_TRUE(original_read_result.is_ok()) << original_read_result.error();

    auto read_result = partition.reader()->Read(mapping.source, mapping_contents);
    ASSERT_TRUE(read_result.is_ok()) << read_result.error();

    EXPECT_TRUE(memcmp(mapping_contents.data(), original_mapping_contents.data(),
                       mapping_contents.size()) == 0);
  }
}

TEST(MinfsPartitionTest, PartitionDataAndReaderIsCorrect) {
  auto fvm_options = MakeFvmOptions(kSliceSize);
  PartitionOptions partition_options;

  auto original_minfs_reader_or = FdReader::Create(kMinfsImagePath);
  ASSERT_TRUE(original_minfs_reader_or.is_ok()) << original_minfs_reader_or.error();
  auto original_minfs_reader = original_minfs_reader_or.take_value();

  auto minfs_reader_or = FdReader::Create(kMinfsImagePath);
  ASSERT_TRUE(minfs_reader_or.is_ok()) << minfs_reader_or.error();
  std::unique_ptr<Reader> minfs_reader = std::make_unique<FdReader>(minfs_reader_or.take_value());

  auto partition_or =
      CreateMinfsFvmPartition(std::move(minfs_reader), partition_options, fvm_options);
  ASSERT_TRUE(partition_or.is_ok()) << partition_or.error();
  auto partition = partition_or.take_value();

  auto superblocks = ReadSuperblocks(partition, original_minfs_reader);
  ASSERT_TRUE(superblocks.has_value());
  auto [original_superblock, actual_superblock] = std::move(superblocks.value());

  const minfs::Superblock* actual_sb =
      reinterpret_cast<minfs::Superblock*>(actual_superblock.data());
  const minfs::Superblock* original_sb =
      reinterpret_cast<minfs::Superblock*>(original_superblock.data());

  ASSERT_NO_FATAL_FAILURE(
      CheckSuperblock(*actual_sb, *original_sb, fvm_options, partition_options));
  ASSERT_NO_FATAL_FAILURE(CheckPartition(partition, *original_sb));
  ASSERT_NO_FATAL_FAILURE(CheckNoNSuperBlockMappingContents(partition, original_minfs_reader));
}

TEST(MinfsPartitionTest, PartitionDataAndReaderIsCorrectWithMinimumInodeCountHigherThanImage) {
  auto fvm_options = MakeFvmOptions(kSliceSize);
  PartitionOptions partition_options;

  auto original_minfs_reader_or = FdReader::Create(kMinfsImagePath);
  ASSERT_TRUE(original_minfs_reader_or.is_ok()) << original_minfs_reader_or.error();
  auto original_minfs_reader = original_minfs_reader_or.take_value();

  minfs::Superblock sb_tmp = {};
  auto read_result = original_minfs_reader.Read(
      0, cpp20::span<uint8_t>(reinterpret_cast<uint8_t*>(&sb_tmp), sizeof(sb_tmp)));
  ASSERT_TRUE(read_result.is_ok()) << read_result.error();

  // Add as many inodes such that at least an extra slice is allocated.
  partition_options.min_inode_count =
      sb_tmp.inode_count + GetBlockCount(0, 3 * fvm_options.slice_size, minfs::kMinfsInodeSize);

  auto minfs_reader_or = FdReader::Create(kMinfsImagePath);
  ASSERT_TRUE(minfs_reader_or.is_ok()) << minfs_reader_or.error();
  std::unique_ptr<Reader> minfs_reader = std::make_unique<FdReader>(minfs_reader_or.take_value());

  auto partition_or =
      CreateMinfsFvmPartition(std::move(minfs_reader), partition_options, fvm_options);
  ASSERT_TRUE(partition_or.is_ok()) << partition_or.error();
  auto partition = partition_or.take_value();

  auto superblocks = ReadSuperblocks(partition, original_minfs_reader);
  ASSERT_TRUE(superblocks.has_value());
  auto [original_superblock, actual_superblock] = std::move(superblocks.value());

  const minfs::Superblock* actual_sb =
      reinterpret_cast<minfs::Superblock*>(actual_superblock.data());
  const minfs::Superblock* original_sb =
      reinterpret_cast<minfs::Superblock*>(original_superblock.data());

  // Adjust the expected inode count such that it matches the amount of slices that would be
  // required for the requested options.
  auto expected_inode_count =
      GetBlockCount(minfs::kFVMBlockInodeStart,
                    minfs::BlocksRequiredForInode(partition_options.min_inode_count.value()) *
                        minfs::kMinfsBlockSize,
                    minfs::kMinfsInodeSize);
  ASSERT_EQ(actual_sb->inode_count, expected_inode_count);
  ASSERT_NO_FATAL_FAILURE(
      CheckSuperblock(*actual_sb, *original_sb, fvm_options, partition_options));
  ASSERT_NO_FATAL_FAILURE(CheckPartition(partition, *original_sb));
  ASSERT_NO_FATAL_FAILURE(CheckNoNSuperBlockMappingContents(partition, original_minfs_reader));
}

TEST(MinfsPartitionTest, PartitionDataAndReaderIsCorrectWithMinimumInodeCountLowerThanImage) {
  auto fvm_options = MakeFvmOptions(kSliceSize);
  PartitionOptions partition_options;

  auto original_minfs_reader_or = FdReader::Create(kMinfsImagePath);
  ASSERT_TRUE(original_minfs_reader_or.is_ok()) << original_minfs_reader_or.error();
  auto original_minfs_reader = original_minfs_reader_or.take_value();

  partition_options.min_inode_count = 1;

  auto minfs_reader_or = FdReader::Create(kMinfsImagePath);
  ASSERT_TRUE(minfs_reader_or.is_ok()) << minfs_reader_or.error();
  std::unique_ptr<Reader> minfs_reader = std::make_unique<FdReader>(minfs_reader_or.take_value());

  auto partition_or =
      CreateMinfsFvmPartition(std::move(minfs_reader), partition_options, fvm_options);
  ASSERT_TRUE(partition_or.is_ok()) << partition_or.error();
  auto partition = partition_or.take_value();

  auto superblocks = ReadSuperblocks(partition, original_minfs_reader);
  ASSERT_TRUE(superblocks.has_value());
  auto [original_superblock, actual_superblock] = std::move(superblocks.value());

  const minfs::Superblock* actual_sb =
      reinterpret_cast<minfs::Superblock*>(actual_superblock.data());
  const minfs::Superblock* original_sb =
      reinterpret_cast<minfs::Superblock*>(original_superblock.data());

  // Adjust the expected inode count such that it matches the amount of slices that would be
  // required for the requested options.
  ASSERT_EQ(actual_sb->inode_count, original_sb->inode_count);
  ASSERT_NO_FATAL_FAILURE(
      CheckSuperblock(*actual_sb, *original_sb, fvm_options, partition_options));
  ASSERT_NO_FATAL_FAILURE(CheckPartition(partition, *original_sb));
  ASSERT_NO_FATAL_FAILURE(CheckNoNSuperBlockMappingContents(partition, original_minfs_reader));
}

TEST(MinfsPartitionTest, PartitionDataAndReaderIsCorrectWithMinimumDataBytesHigherThanImage) {
  auto fvm_options = MakeFvmOptions(kSliceSize);
  PartitionOptions partition_options;

  auto original_minfs_reader_or = FdReader::Create(kMinfsImagePath);
  ASSERT_TRUE(original_minfs_reader_or.is_ok()) << original_minfs_reader_or.error();
  auto original_minfs_reader = original_minfs_reader_or.take_value();

  minfs::Superblock sb_tmp = {};
  auto read_result = original_minfs_reader.Read(
      0, cpp20::span<uint8_t>(reinterpret_cast<uint8_t*>(&sb_tmp), sizeof(sb_tmp)));
  ASSERT_TRUE(read_result.is_ok()) << read_result.error();

  // Add as many inodes such that at least an extra slice is allocated.
  partition_options.min_data_bytes = sb_tmp.block_count + 3 * fvm_options.slice_size;

  auto minfs_reader_or = FdReader::Create(kMinfsImagePath);
  ASSERT_TRUE(minfs_reader_or.is_ok()) << minfs_reader_or.error();
  std::unique_ptr<Reader> minfs_reader = std::make_unique<FdReader>(minfs_reader_or.take_value());

  auto partition_or =
      CreateMinfsFvmPartition(std::move(minfs_reader), partition_options, fvm_options);
  ASSERT_TRUE(partition_or.is_ok()) << partition_or.error();
  auto partition = partition_or.take_value();

  auto superblocks = ReadSuperblocks(partition, original_minfs_reader);
  ASSERT_TRUE(superblocks.has_value());
  auto [original_superblock, actual_superblock] = std::move(superblocks.value());

  const minfs::Superblock* actual_sb =
      reinterpret_cast<minfs::Superblock*>(actual_superblock.data());
  const minfs::Superblock* original_sb =
      reinterpret_cast<minfs::Superblock*>(original_superblock.data());

  // Round to fvm slices, then to minfs blocks.
  auto expected_data_block_count = GetBlockCount(
      0,
      GetBlockCount(minfs::kFVMBlockDataStart, partition_options.min_data_bytes.value(),
                    fvm_options.slice_size) *
          fvm_options.slice_size,
      minfs::kMinfsBlockSize);
  ASSERT_EQ(actual_sb->block_count, expected_data_block_count);
  ASSERT_NO_FATAL_FAILURE(
      CheckSuperblock(*actual_sb, *original_sb, fvm_options, partition_options));
  ASSERT_NO_FATAL_FAILURE(CheckPartition(partition, *original_sb));
  ASSERT_NO_FATAL_FAILURE(CheckNoNSuperBlockMappingContents(partition, original_minfs_reader));
}

TEST(MinfsPartitionTest, PartitionDataAndReaderIsCorrectWithMinimumDataBytesLowerThanImage) {
  auto fvm_options = MakeFvmOptions(kSliceSize);
  PartitionOptions partition_options;

  auto original_minfs_reader_or = FdReader::Create(kMinfsImagePath);
  ASSERT_TRUE(original_minfs_reader_or.is_ok()) << original_minfs_reader_or.error();
  auto original_minfs_reader = original_minfs_reader_or.take_value();

  auto minfs_reader_or = FdReader::Create(kMinfsImagePath);
  ASSERT_TRUE(minfs_reader_or.is_ok()) << minfs_reader_or.error();
  std::unique_ptr<Reader> minfs_reader = std::make_unique<FdReader>(minfs_reader_or.take_value());

  auto partition_or =
      CreateMinfsFvmPartition(std::move(minfs_reader), partition_options, fvm_options);
  ASSERT_TRUE(partition_or.is_ok()) << partition_or.error();
  auto partition = partition_or.take_value();

  auto superblocks = ReadSuperblocks(partition, original_minfs_reader);
  ASSERT_TRUE(superblocks.has_value());
  auto [original_superblock, actual_superblock] = std::move(superblocks.value());

  const minfs::Superblock* actual_sb =
      reinterpret_cast<minfs::Superblock*>(actual_superblock.data());
  const minfs::Superblock* original_sb =
      reinterpret_cast<minfs::Superblock*>(original_superblock.data());

  ASSERT_NO_FATAL_FAILURE(
      CheckSuperblock(*actual_sb, *original_sb, fvm_options, partition_options));
  ASSERT_NO_FATAL_FAILURE(CheckPartition(partition, *original_sb));
  ASSERT_NO_FATAL_FAILURE(CheckNoNSuperBlockMappingContents(partition, original_minfs_reader));
}

TEST(MinfsPartitionTest, PartitionDataAndReaderIsCorrectWithMaxBytesHigherThanImage) {
  auto fvm_options = MakeFvmOptions(kSliceSize);
  PartitionOptions partition_options;

  auto original_minfs_reader_or = FdReader::Create(kMinfsImagePath);
  ASSERT_TRUE(original_minfs_reader_or.is_ok()) << original_minfs_reader_or.error();
  auto original_minfs_reader = original_minfs_reader_or.take_value();

  partition_options.max_bytes = std::numeric_limits<uint64_t>::max();

  auto minfs_reader_or = FdReader::Create(kMinfsImagePath);
  ASSERT_TRUE(minfs_reader_or.is_ok()) << minfs_reader_or.error();
  std::unique_ptr<Reader> minfs_reader = std::make_unique<FdReader>(minfs_reader_or.take_value());

  auto partition_or =
      CreateMinfsFvmPartition(std::move(minfs_reader), partition_options, fvm_options);
  ASSERT_TRUE(partition_or.is_ok()) << partition_or.error();
  auto partition = partition_or.take_value();

  auto superblocks = ReadSuperblocks(partition, original_minfs_reader);
  ASSERT_TRUE(superblocks.has_value());
  auto [original_superblock, actual_superblock] = std::move(superblocks.value());

  const minfs::Superblock* actual_sb =
      reinterpret_cast<minfs::Superblock*>(actual_superblock.data());
  const minfs::Superblock* original_sb =
      reinterpret_cast<minfs::Superblock*>(original_superblock.data());

  // Adjust the expected inode count such that it matches the amount of slices that would be
  // required for the requested options.
  ASSERT_NO_FATAL_FAILURE(
      CheckSuperblock(*actual_sb, *original_sb, fvm_options, partition_options));
  ASSERT_NO_FATAL_FAILURE(CheckPartition(partition, *original_sb));
  ASSERT_NO_FATAL_FAILURE(CheckNoNSuperBlockMappingContents(partition, original_minfs_reader));
}

TEST(MinfsPartitionTest, ExceedingMaxBytesIsError) {
  auto fvm_options = MakeFvmOptions(kSliceSize);
  PartitionOptions partition_options;

  auto original_minfs_reader_or = FdReader::Create(kMinfsImagePath);
  ASSERT_TRUE(original_minfs_reader_or.is_ok()) << original_minfs_reader_or.error();
  auto original_minfs_reader = original_minfs_reader_or.take_value();

  partition_options.max_bytes = 1;

  auto minfs_reader_or = FdReader::Create(kMinfsImagePath);
  ASSERT_TRUE(minfs_reader_or.is_ok()) << minfs_reader_or.error();
  std::unique_ptr<Reader> minfs_reader = std::make_unique<FdReader>(minfs_reader_or.take_value());

  auto partition_or =
      CreateMinfsFvmPartition(std::move(minfs_reader), partition_options, fvm_options);
  ASSERT_TRUE(partition_or.is_error());
}

}  // namespace
}  // namespace storage::volume_image
