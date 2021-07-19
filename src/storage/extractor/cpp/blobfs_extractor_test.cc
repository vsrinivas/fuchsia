// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
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

#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/test/blob_utils.h"
#include "src/storage/blobfs/test/integration/blobfs_fixtures.h"
#include "src/storage/extractor/c/extractor.h"
#include "src/storage/extractor/cpp/extractor.h"
#include "src/storage/fs_test/blobfs_test.h"
#include "src/storage/fs_test/fs_test.h"
#include "src/storage/fs_test/fs_test_fixture.h"

namespace extractor {
namespace {

using BlobfsExtractionTest = fs_test::FilesystemTest;

constexpr uint64_t kExtractedImageBlockCount = 2;
constexpr uint64_t kExtractedImageStartOffset = 0;

constexpr uint64_t SuperblockOffset() {
  return kExtractedImageStartOffset + kExtractedImageBlockCount * blobfs::kBlobfsBlockSize;
}

constexpr uint64_t BlockBitmapOffset(const blobfs::Superblock& info) {
  uint64_t block_map_offset =
      SuperblockOffset() + blobfs::kBlobfsSuperblockBlocks * blobfs::kBlobfsBlockSize;
  if (info.flags & blobfs::kBlobFlagFVM) {
    block_map_offset += blobfs::kBlobfsSuperblockBlocks;
  }
  return block_map_offset;
}

constexpr uint64_t NodemapOffset(const blobfs::Superblock& info) {
  return BlockBitmapOffset(info) + blobfs::BlockMapBlocks(info) * blobfs::kBlobfsBlockSize;
}

constexpr uint64_t JournalOffset(const blobfs::Superblock& info) {
  return NodemapOffset(info) + blobfs::NodeMapBlocks(info) * blobfs::kBlobfsBlockSize;
}

void CreateInputAndOutputStream(fs_test::TestFilesystem& fs, fbl::unique_fd& input,
                                fbl::unique_fd& output) {
  std::unique_ptr<blobfs::BlobInfo> blob_info =
      blobfs::GenerateBlob(blobfs::RandomFill, fs.mount_path(), 1 << 17);
  fbl::unique_fd fd;
  ASSERT_NO_FATAL_FAILURE(MakeBlob(*blob_info, &fd));
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(fs.Unmount().status_value(), ZX_OK);
  input.reset(open(fs.DevicePath().value().c_str(), O_RDONLY));
  ASSERT_TRUE(input);
  char out_path[] = "/tmp/blobfs-extraction.XXXXXX";
  output.reset(mkostemp(out_path, O_RDWR | O_CREAT | O_EXCL));
  ASSERT_TRUE(output);
}

void Extract(const fbl::unique_fd& input_fd, const fbl::unique_fd& output_fd) {
  ExtractorOptions options = ExtractorOptions{.force_dump_pii = false,
                                              .add_checksum = false,
                                              .alignment = blobfs::kBlobfsBlockSize,
                                              .compress = false};
  auto extractor_status = Extractor::Create(input_fd.duplicate(), options, output_fd.duplicate());
  ASSERT_EQ(extractor_status.status_value(), ZX_OK);
  auto extractor = std::move(extractor_status.value());
  auto status = BlobfsExtract(input_fd.duplicate(), *extractor);
  ASSERT_TRUE(status.is_ok());
  ASSERT_TRUE(extractor->Write().is_ok());
}

void VerifyInputSuperblock(blobfs::Superblock* info, const fbl::unique_fd& input_fd) {
  ASSERT_EQ(blobfs::kBlobfsBlockSize,
            pread(input_fd.get(), info, blobfs::kBlobfsBlockSize, blobfs::kSuperblockOffset));
  ASSERT_EQ(info->magic0, blobfs::kBlobfsMagic0);
  ASSERT_EQ(info->magic1, blobfs::kBlobfsMagic1);
}

void VerifyOutputSuperblock(blobfs::Superblock* info, const fbl::unique_fd& output_fd) {
  ssize_t superblock_offset = SuperblockOffset();

  char read_buffer[blobfs::kBlobfsBlockSize];
  ASSERT_EQ(pread(output_fd.get(), read_buffer, sizeof(read_buffer), superblock_offset),
            static_cast<ssize_t>(sizeof(read_buffer)));
  ASSERT_EQ(memcmp(info, read_buffer, sizeof(read_buffer)), 0);
}

TEST_P(BlobfsExtractionTest, TestSuperblock) {
  fbl::unique_fd input_fd;
  fbl::unique_fd output_fd;

  CreateInputAndOutputStream(fs(), input_fd, output_fd);
  Extract(input_fd, output_fd);

  blobfs::Superblock info;
  VerifyInputSuperblock(&info, input_fd);

  struct stat stats;
  ASSERT_EQ(fstat(output_fd.get(), &stats), 0);
  VerifyOutputSuperblock(&info, output_fd);
}

TEST_P(BlobfsExtractionTest, TestNodeMap) {
  fbl::unique_fd input_fd;
  fbl::unique_fd output_fd;

  CreateInputAndOutputStream(fs(), input_fd, output_fd);
  Extract(input_fd, output_fd);

  blobfs::Superblock info;
  VerifyInputSuperblock(&info, input_fd);
  ASSERT_EQ(info.alloc_inode_count, 1ul);

  VerifyOutputSuperblock(&info, output_fd);

  std::unique_ptr<blobfs::Inode[]> inode_table;
  inode_table =
      std::make_unique<blobfs::Inode[]>(NodeMapBlocks(info) * blobfs::kBlobfsInodesPerBlock);
  ssize_t size = blobfs::NodeMapBlocks(info) * blobfs::kBlobfsBlockSize;
  ASSERT_EQ(pread(input_fd.get(), inode_table.get(), size,
                  blobfs::kBlobfsBlockSize * blobfs::NodeMapStartBlock(info)),
            size);

  char read_buffer_nodemap[size];
  ASSERT_EQ(pread(output_fd.get(), read_buffer_nodemap, size, NodemapOffset(info)), size);
  ASSERT_EQ(memcmp(inode_table.get(), read_buffer_nodemap, size), 0);
}

TEST_P(BlobfsExtractionTest, TestBlockMap) {
  fbl::unique_fd input_fd;
  fbl::unique_fd output_fd;

  CreateInputAndOutputStream(fs(), input_fd, output_fd);
  Extract(input_fd, output_fd);

  blobfs::Superblock info;
  VerifyInputSuperblock(&info, input_fd);
  ASSERT_EQ(info.alloc_inode_count, 1ul);

  VerifyOutputSuperblock(&info, output_fd);

  ssize_t size = blobfs::BlockMapBlocks(info) * blobfs::kBlobfsBlockSize;
  char block_bitmap[size];
  ASSERT_EQ(pread(input_fd.get(), block_bitmap, size,
                  blobfs::kBlobfsBlockSize * blobfs::BlockMapStartBlock(info)),
            size);

  char read_buffer_blockmap[size];
  ASSERT_EQ(pread(output_fd.get(), read_buffer_blockmap, size, BlockBitmapOffset(info)), size);
  ASSERT_EQ(memcmp(block_bitmap, read_buffer_blockmap, size), 0);
}

TEST_P(BlobfsExtractionTest, TestJournal) {
  fbl::unique_fd input_fd;
  fbl::unique_fd output_fd;

  CreateInputAndOutputStream(fs(), input_fd, output_fd);
  Extract(input_fd, output_fd);

  blobfs::Superblock info;

  VerifyInputSuperblock(&info, input_fd);
  ASSERT_EQ(info.alloc_inode_count, 1ul);

  VerifyOutputSuperblock(&info, output_fd);

  ssize_t size = blobfs::JournalBlocks(info) * blobfs::kBlobfsBlockSize;
  std::unique_ptr<char[]> journal(new char[size]);
  ASSERT_EQ(pread(input_fd.get(), journal.get(), size,
                  blobfs::kBlobfsBlockSize * blobfs::JournalStartBlock(info)),
            size);

  std::unique_ptr<char[]> read_buffer_blockmap(new char[size]);
  ASSERT_EQ(pread(output_fd.get(), read_buffer_blockmap.get(), size, JournalOffset(info)), size);
  ASSERT_EQ(memcmp(journal.get(), read_buffer_blockmap.get(), size), 0);
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, BlobfsExtractionTest,
                         testing::Values(blobfs::BlobfsDefaultTestParam()),
                         testing::PrintToStringParamName());
}  // namespace

}  // namespace extractor
