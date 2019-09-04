// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/host.h>

#include <stdio.h>

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

}  // namespace
}  // namespace blobfs
