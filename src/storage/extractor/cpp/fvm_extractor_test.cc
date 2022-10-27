// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/spawn.h>
#include <lib/zx/process.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zircon/device/block.h>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <string>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/lib/storage/fs_management/cpp/fvm.h"
#include "src/storage/extractor/c/extractor.h"
#include "src/storage/extractor/cpp/extractor.h"
#include "src/storage/fvm/format.h"
#include "src/storage/testing/ram_disk.h"

namespace extractor {
namespace {

constexpr uint32_t kBlockCount = 1024 * 256;
constexpr uint32_t kBlockSize = 8192;
constexpr uint32_t kSliceSize = 32'768;
constexpr uint64_t kExtractedImageBlockCount = 2;

void CreateInputAndOutputStream(storage::RamDisk& ramdisk, fbl::unique_fd& input,
                                fbl::unique_fd& output) {
  input.reset(open(ramdisk.path().c_str(), O_RDWR));

  fdio_cpp::UnownedFdioCaller caller(input);
  ASSERT_EQ(fs_management::FvmInitPreallocated(caller.borrow_as<fuchsia_hardware_block::Block>(),
                                               static_cast<uint64_t>(kBlockCount) * kBlockSize,
                                               static_cast<uint64_t>(kBlockCount) * kBlockSize,
                                               kSliceSize),
            ZX_OK);
  ASSERT_TRUE(input);
  char out_path[] = "/tmp/fvm-extraction.XXXXXX";
  output.reset(mkostemp(out_path, O_RDWR | O_CREAT | O_EXCL));
  ASSERT_TRUE(output);
}

void Extract(const fbl::unique_fd& input_fd, const fbl::unique_fd& output_fd, bool corrupt) {
  ExtractorOptions options = ExtractorOptions{.force_dump_pii = false,
                                              .add_checksum = false,
                                              .alignment = fvm::kBlockSize,
                                              .compress = false};
  auto extractor_status = Extractor::Create(input_fd.duplicate(), options, output_fd.duplicate());
  ASSERT_EQ(extractor_status.status_value(), ZX_OK);
  auto extractor = std::move(extractor_status.value());
  auto status = FvmExtract(input_fd.duplicate(), *extractor);
  if (!corrupt) {
    ASSERT_TRUE(status.is_ok());
  }
  ASSERT_TRUE(extractor->Write().is_ok());
}

void VerifyInputSuperblock(const fbl::unique_fd& input_fd, fvm::Header* info) {
  // First copy
  char buffer[fvm::kBlockSize];
  ASSERT_EQ((ssize_t)fvm::kBlockSize, pread(input_fd.get(), buffer, fvm::kBlockSize, 0));
  memcpy(info, buffer, sizeof(*info));
  ASSERT_EQ(info->magic, fvm::kMagic);

  // Second copy
  char second_buffer[fvm::kBlockSize];
  fvm::Header secondary_superblock;
  ASSERT_EQ((ssize_t)fvm::kBlockSize,
            pread(input_fd.get(), second_buffer, fvm::kBlockSize,
                  info->GetSuperblockOffset(fvm::SuperblockType::kSecondary)));
  memcpy(&secondary_superblock, second_buffer, sizeof(*info));
  ASSERT_EQ(secondary_superblock.magic, fvm::kMagic);
}

void VerifyOutputSuperblock(const fvm::Header& info, const fbl::unique_fd& output_fd) {
  ssize_t superblock_offset = kExtractedImageBlockCount * fvm::kBlockSize;
  char read_buffer[fvm::kBlockSize];
  ASSERT_EQ(pread(output_fd.get(), read_buffer, fvm::kBlockSize, superblock_offset),
            static_cast<ssize_t>(fvm::kBlockSize));
  char info_buffer[fvm::kBlockSize];
  memcpy(info_buffer, &info, sizeof(info));
  ASSERT_EQ(memcmp(info_buffer, read_buffer, sizeof(info)), 0);
}

TEST(FvmExtractorTest, TestSuperblock) {
  fbl::unique_fd input_fd;
  fbl::unique_fd output_fd;
  auto ramdisk_or = storage::RamDisk::Create(kBlockSize, kBlockCount);
  ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);

  CreateInputAndOutputStream(ramdisk_or.value(), input_fd, output_fd);
  Extract(input_fd, output_fd, /*corrupt=*/false);

  fvm::Header info;
  VerifyInputSuperblock(input_fd, &info);

  struct stat stats;
  ASSERT_EQ(fstat(output_fd.get(), &stats), 0);
  VerifyOutputSuperblock(info, output_fd);
}

TEST(FvmExtractorTest, TestCorruptedSuperblock) {
  fbl::unique_fd input_fd;
  fbl::unique_fd output_fd;
  auto ramdisk_or = storage::RamDisk::Create(kBlockSize, kBlockCount);
  ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);

  CreateInputAndOutputStream(ramdisk_or.value(), input_fd, output_fd);
  char corrupt_block[fvm::kBlockSize] = {'C'};
  ssize_t r = pwrite(input_fd.get(), corrupt_block, sizeof(corrupt_block), 0);
  ASSERT_EQ(r, (ssize_t)sizeof(corrupt_block));
  Extract(input_fd, output_fd, /*corrupt=*/true);

  std::unique_ptr<char[]> superblock(new char[fvm::kBlockSize]);
  ASSERT_EQ(pread(input_fd.get(), superblock.get(), fvm::kBlockSize, 0), (ssize_t)fvm::kBlockSize);
  std::unique_ptr<char[]> read_buffer_sb(new char[fvm::kBlockSize]);
  ASSERT_EQ(pread(output_fd.get(), read_buffer_sb.get(), fvm::kBlockSize,
                  kExtractedImageBlockCount * fvm::kBlockSize),
            (ssize_t)fvm::kBlockSize);
  ASSERT_EQ(memcmp(superblock.get(), corrupt_block, fvm::kBlockSize), 0);
  ASSERT_EQ(memcmp(read_buffer_sb.get(), corrupt_block, fvm::kBlockSize), 0);
}

TEST(FvmExtractorTest, TestMetadata) {
  fbl::unique_fd input_fd;
  fbl::unique_fd output_fd;
  auto ramdisk_or = storage::RamDisk::Create(kBlockSize, kBlockCount);
  ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);

  CreateInputAndOutputStream(ramdisk_or.value(), input_fd, output_fd);
  Extract(input_fd, output_fd, /*corrupt=*/false);

  fvm::Header info;
  VerifyInputSuperblock(input_fd, &info);
  VerifyOutputSuperblock(info, output_fd);
  ssize_t used_size = static_cast<ssize_t>(2 * info.GetMetadataUsedBytes());
  ssize_t allocated_size = static_cast<ssize_t>(2 * info.GetMetadataAllocatedBytes());
  std::unique_ptr<char[]> metadata(new char[allocated_size]);
  ASSERT_EQ(pread(input_fd.get(), metadata.get(), allocated_size, 0), allocated_size);
  std::unique_ptr<char[]> read_buffer_nodemap(new char[used_size]);
  ASSERT_EQ(pread(output_fd.get(), read_buffer_nodemap.get(), used_size,
                  kExtractedImageBlockCount * fvm::kBlockSize),
            used_size);
  ASSERT_EQ(memcmp(metadata.get(), read_buffer_nodemap.get(), info.GetMetadataUsedBytes()), 0);
  ASSERT_EQ(
      memcmp(metadata.get() + info.GetMetadataAllocatedBytes(),
             read_buffer_nodemap.get() + info.GetMetadataUsedBytes(), info.GetMetadataUsedBytes()),
      0);
}

TEST(FvmExtractorTest, TestPartitionTable) {
  fbl::unique_fd input_fd;
  fbl::unique_fd output_fd;
  auto ramdisk_or = storage::RamDisk::Create(kBlockSize, kBlockCount);
  ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);

  CreateInputAndOutputStream(ramdisk_or.value(), input_fd, output_fd);
  Extract(input_fd, output_fd, /*corrupt=*/false);

  fvm::Header info;
  VerifyInputSuperblock(input_fd, &info);
  VerifyOutputSuperblock(info, output_fd);

  // First copy
  std::unique_ptr<char[]> partition_table(new char[info.GetPartitionTableByteSize()]);
  ASSERT_EQ(pread(input_fd.get(), partition_table.get(), info.GetPartitionTableByteSize(),
                  info.GetPartitionTableOffset()),
            (ssize_t)info.GetPartitionTableByteSize());
  std::unique_ptr<char[]> read_buffer_pt(new char[info.GetPartitionTableByteSize()]);
  ASSERT_EQ(pread(output_fd.get(), read_buffer_pt.get(), info.GetPartitionTableByteSize(),
                  kExtractedImageBlockCount * fvm::kBlockSize + info.GetPartitionTableOffset()),
            (ssize_t)info.GetPartitionTableByteSize());

  ASSERT_EQ(memcmp(partition_table.get(), read_buffer_pt.get(), info.GetPartitionTableByteSize()),
            0);

  // Second copy
  size_t secondarySuperblockOffset = info.GetSuperblockOffset(fvm::SuperblockType::kSecondary);
  size_t used_allocated_diff =
      info.GetAllocationTableAllocatedByteSize() - info.GetAllocationTableUsedByteSize();

  std::unique_ptr<char[]> second_partition_table(new char[info.GetPartitionTableByteSize()]);
  ASSERT_EQ(pread(input_fd.get(), second_partition_table.get(), info.GetPartitionTableByteSize(),
                  secondarySuperblockOffset + info.GetPartitionTableOffset()),
            (ssize_t)info.GetPartitionTableByteSize());
  std::unique_ptr<char[]> read_buffer_pt_2(new char[info.GetPartitionTableByteSize()]);
  ASSERT_EQ(pread(output_fd.get(), read_buffer_pt_2.get(), info.GetPartitionTableByteSize(),
                  kExtractedImageBlockCount * fvm::kBlockSize + secondarySuperblockOffset -
                      used_allocated_diff + info.GetPartitionTableOffset()),
            (ssize_t)info.GetPartitionTableByteSize());

  ASSERT_EQ(memcmp(second_partition_table.get(), read_buffer_pt_2.get(),
                   info.GetPartitionTableByteSize()),
            0);
}

TEST(FvmExtractorTest, TestAllocationTable) {
  fbl::unique_fd input_fd;
  fbl::unique_fd output_fd;
  auto ramdisk_or = storage::RamDisk::Create(kBlockSize, kBlockCount);
  ASSERT_EQ(ramdisk_or.status_value(), ZX_OK);

  CreateInputAndOutputStream(ramdisk_or.value(), input_fd, output_fd);
  Extract(input_fd, output_fd, /*corrupt=*/false);

  fvm::Header info;
  VerifyInputSuperblock(input_fd, &info);
  VerifyOutputSuperblock(info, output_fd);

  // First copy
  std::unique_ptr<char[]> allocation_table(new char[info.GetAllocationTableUsedByteSize()]);
  ASSERT_EQ(pread(input_fd.get(), allocation_table.get(), info.GetAllocationTableUsedByteSize(),
                  info.GetAllocationTableOffset()),
            (ssize_t)info.GetAllocationTableUsedByteSize());
  std::unique_ptr<char[]> read_buffer_at(new char[info.GetAllocationTableUsedByteSize()]);
  ASSERT_EQ(pread(output_fd.get(), read_buffer_at.get(), info.GetAllocationTableUsedByteSize(),
                  kExtractedImageBlockCount * fvm::kBlockSize + info.GetAllocationTableOffset()),
            (ssize_t)info.GetAllocationTableUsedByteSize());

  ASSERT_EQ(
      memcmp(allocation_table.get(), read_buffer_at.get(), info.GetAllocationTableUsedByteSize()),
      0);

  // Second copy
  size_t secondarySuperblockOffset = info.GetSuperblockOffset(fvm::SuperblockType::kSecondary);
  size_t used_allocated_diff =
      info.GetAllocationTableAllocatedByteSize() - info.GetAllocationTableUsedByteSize();
  std::unique_ptr<char[]> second_allocation_table(new char[info.GetAllocationTableUsedByteSize()]);
  ASSERT_EQ(
      pread(input_fd.get(), second_allocation_table.get(), info.GetAllocationTableUsedByteSize(),
            secondarySuperblockOffset + info.GetAllocationTableOffset()),
      (ssize_t)info.GetAllocationTableUsedByteSize());
  std::unique_ptr<char[]> read_buffer_at_2(new char[info.GetAllocationTableUsedByteSize()]);
  ASSERT_EQ(pread(output_fd.get(), read_buffer_at_2.get(), info.GetAllocationTableUsedByteSize(),
                  kExtractedImageBlockCount * fvm::kBlockSize + secondarySuperblockOffset -
                      used_allocated_diff + info.GetAllocationTableOffset()),
            (ssize_t)info.GetAllocationTableUsedByteSize());

  ASSERT_EQ(memcmp(second_allocation_table.get(), read_buffer_at_2.get(),
                   info.GetAllocationTableUsedByteSize()),
            0);
}

}  // namespace

}  // namespace extractor
