// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include <blobfs/host.h>
#include <zxtest/zxtest.h>

namespace blobfs {
namespace {

class File {
 public:
  explicit File(FILE* file) : file_(file) {}
  File(const File&) = delete;
  File& operator=(const File&) = delete;
  File(File&&) = delete;
  File& operator=(File&&) = delete;

  int fd() const { return fileno(file_); }

  ~File() { fclose(file_); }

 private:
  FILE* file_;
};

TEST(BlobfsHostFormatTest, FormatDevice) {
  File file(tmpfile());
  EXPECT_EQ(0, Mkfs(file.fd(), 10000));
}

TEST(BlobfsHostFormatTest, FormatZeroBlockDevice) {
  File file(tmpfile());
  EXPECT_EQ(-1, Mkfs(file.fd(), 0));
}

TEST(BlobfsHostFormatTest, FormatTooSmallDevice) {
  File file(tmpfile());
  EXPECT_EQ(-1, Mkfs(file.fd(), 1));
}

// This test verifies that formatting actually writes zero-filled
// blocks within the journal.
TEST(BlobfsHostFormatTest, JournalFormattedAsEmpty) {
  File file(tmpfile());
  constexpr uint64_t kBlockCount = 10000;
  EXPECT_EQ(0, Mkfs(file.fd(), kBlockCount));

  char block[kBlobfsBlockSize] = {};
  ASSERT_OK(ReadBlock(file.fd(), 0, block));
  static_assert(sizeof(Superblock) <= sizeof(block), "Superblock too big");
  const Superblock* superblock = reinterpret_cast<Superblock*>(block);
  ASSERT_OK(CheckSuperblock(superblock, kBlockCount));

  uint64_t journal_blocks = JournalBlocks(*superblock);
  char zero_block[kBlobfsBlockSize] = {};

  // '1' -> Skip the journal info block.
  for (uint64_t n = 1; n < journal_blocks; n++) {
    char block[kBlobfsBlockSize] = {};
    ASSERT_OK(ReadBlock(file.fd(), JournalStartBlock(*superblock) + n, block));
    EXPECT_BYTES_EQ(zero_block, block, kBlobfsBlockSize, "Journal should be formatted with zeros");
  }
}

// Verify that we compress small files.
TEST(BlobfsHostCompressionTest, CompressSmallFiles) {
  File fs_file(tmpfile());
  EXPECT_EQ(0, Mkfs(fs_file.fd(), 10000));

  constexpr size_t all_zero_size = 12 * 1024;
  File blob_file(tmpfile());
  EXPECT_EQ(0, ftruncate(blob_file.fd(), all_zero_size));

  constexpr bool compress = true;
  MerkleInfo info;
  EXPECT_EQ(ZX_OK, blobfs_preprocess(blob_file.fd(), compress, &info));

  EXPECT_TRUE(info.compressed);
  EXPECT_LE(info.compressed_length, all_zero_size);
}

}  // namespace
}  // namespace blobfs
