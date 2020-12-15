// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/host.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <zircon/assert.h>

#include <cstring>
#include <limits>
#include <memory>
#include <optional>

#include <digest/digest.h>
#include <digest/node-digest.h>
#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/storage/blobfs/blob-layout.h"
#include "src/storage/blobfs/blobfs-checker.h"
#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/node-finder.h"

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

std::unique_ptr<Blobfs> CreateBlobfs(uint64_t block_count, FilesystemOptions options) {
  File fs_file(tmpfile());
  if (ftruncate(fs_file.fd(), block_count * kBlobfsBlockSize) == -1) {
    ADD_FAILURE() << "Failed to resize the file for " << block_count << " blocks";
    return nullptr;
  }
  if (Mkfs(fs_file.fd(), block_count, options) == -1) {
    ADD_FAILURE() << "Mkfs failed";
    return nullptr;
  }
  fbl::unique_fd fs_fd(dup(fs_file.fd()));
  std::unique_ptr<Blobfs> blobfs;
  zx_status_t status;
  if ((status = blobfs_create(&blobfs, std::move(fs_fd))) != ZX_OK) {
    ADD_FAILURE() << "blobfs_created returned: " << status;
    return nullptr;
  }
  return blobfs;
}

std::optional<Inode> FindInodeByMerkleDigest(Blobfs& blobfs, digest::Digest& digest) {
  for (uint32_t i = 0; i < blobfs.Info().alloc_inode_count; ++i) {
    InodePtr inode = blobfs.GetNode(i);
    if (inode == nullptr) {
      return std::nullopt;
    }
    if (!inode->header.IsAllocated() || !inode->header.IsInode()) {
      continue;
    }
    if (digest == inode->merkle_root_hash) {
      return *inode;
    }
  }
  return std::nullopt;
}

void FillFileWithRandomContent(File& file, size_t size, unsigned int* seed) {
  std::vector<uint8_t> file_contents(size, 0);

  for (auto& b : file_contents) {
    b = rand_r(seed) % std::numeric_limits<uint8_t>::max();
  }

  int written = 0;
  int write_result = 0;
  while ((write_result = write(file.fd(), file_contents.data() + written,
                               file_contents.size() - written)) > 0) {
    written += write_result;
  }
  ASSERT_EQ(write_result, 0);
  ASSERT_EQ(written, static_cast<int>(size));
}

void InitBlob(uint64_t data_size, Blobfs& blobfs, File& blob, MerkleInfo& info,
              unsigned int* seed) {
  EXPECT_EQ(ftruncate(blob.fd(), data_size), 0);
  FillFileWithRandomContent(blob, data_size, seed);
  EXPECT_EQ(blobfs_add_blob(&blobfs, /*json_recorder=*/nullptr, blob.fd()), ZX_OK);
  EXPECT_EQ(blobfs_preprocess(blob.fd(), false, GetBlobLayoutFormat(blobfs.Info()), &info), ZX_OK);
}

// Adds an uncompressed blob of size |data_size| to |blobfs| and returns the created blob's Inode.
Inode AddUncompressedBlob(uint64_t data_size, Blobfs& blobfs) {
  File blob_file(tmpfile());
  MerkleInfo info;
  unsigned int seed = testing::UnitTest::GetInstance()->random_seed();
  InitBlob(data_size, blobfs, blob_file, info, &seed);
  return FindInodeByMerkleDigest(blobfs, info.digest).value();
}

// Adds a compressed blob with an uncompressed size of |data_size| to |blobfs| and returns the
// created blob's Inode.  The blobs data will be all zeros which will be significantly compressed.
Inode AddCompressedBlob(uint64_t data_size, Blobfs& blobfs) {
  File blob_file(tmpfile());
  EXPECT_EQ(ftruncate(blob_file.fd(), data_size), 0);
  MerkleInfo info;
  EXPECT_EQ(blobfs_preprocess(blob_file.fd(), true, GetBlobLayoutFormat(blobfs.Info()), &info),
            ZX_OK);
  // Make sure that the blob was compressed.
  EXPECT_TRUE(info.compressed);
  EXPECT_EQ(blobfs_add_blob_with_merkle(&blobfs, /*json_recorder=*/nullptr, blob_file.fd(), info),
            ZX_OK);
  return FindInodeByMerkleDigest(blobfs, info.digest).value();
}

TEST(BlobfsHostFormatTest, FormatDevice) {
  File file(tmpfile());
  EXPECT_EQ(Mkfs(file.fd(), 10000, FilesystemOptions{}), 0);
}

TEST(BlobfsHostFormatTest, FormatZeroBlockDevice) {
  File file(tmpfile());
  EXPECT_EQ(Mkfs(file.fd(), 0, FilesystemOptions{}), -1);
}

TEST(BlobfsHostFormatTest, FormatTooSmallDevice) {
  File file(tmpfile());
  EXPECT_EQ(Mkfs(file.fd(), 1, FilesystemOptions{}), -1);
}

// This test verifies that formatting actually writes zero-filled
// blocks within the journal.
TEST(BlobfsHostFormatTest, JournalFormattedAsEmpty) {
  File file(tmpfile());
  constexpr uint64_t kBlockCount = 10000;
  EXPECT_EQ(Mkfs(file.fd(), kBlockCount, FilesystemOptions{}), 0);

  char block[kBlobfsBlockSize] = {};
  ASSERT_EQ(ReadBlock(file.fd(), 0, block), ZX_OK);
  static_assert(sizeof(Superblock) <= sizeof(block), "Superblock too big");
  const Superblock* superblock = reinterpret_cast<Superblock*>(block);
  ASSERT_EQ(CheckSuperblock(superblock, kBlockCount), ZX_OK);

  uint64_t journal_blocks = JournalBlocks(*superblock);
  char zero_block[kBlobfsBlockSize] = {};

  // '1' -> Skip the journal info block.
  for (uint64_t n = 1; n < journal_blocks; n++) {
    char block[kBlobfsBlockSize] = {};
    ASSERT_EQ(ReadBlock(file.fd(), JournalStartBlock(*superblock) + n, block), ZX_OK);
    EXPECT_EQ(memcmp(zero_block, block, kBlobfsBlockSize), 0)
        << "Journal should be formatted with zeros";
  }
}

// Verify that we compress small files.
TEST(BlobfsHostCompressionTest, CompressSmallFiles) {
  File fs_file(tmpfile());
  EXPECT_EQ(Mkfs(fs_file.fd(), 10000, FilesystemOptions{}), 0);

  constexpr size_t all_zero_size = 12 * 1024;
  File blob_file(tmpfile());
  EXPECT_EQ(ftruncate(blob_file.fd(), all_zero_size), 0);

  constexpr bool compress = true;
  MerkleInfo info;
  EXPECT_EQ(blobfs_preprocess(blob_file.fd(), compress, BlobLayoutFormat::kPaddedMerkleTreeAtStart,
                              &info),
            ZX_OK);

  EXPECT_TRUE(info.compressed);
  EXPECT_LE(info.compressed_length, all_zero_size);
}

TEST(BlobfsHostTest, WriteBlobWithPaddedFormatIsCorrect) {
  auto blobfs = CreateBlobfs(/*block_count=*/500,
                             {.blob_layout_format = BlobLayoutFormat::kPaddedMerkleTreeAtStart});
  ASSERT_TRUE(blobfs != nullptr);

  // In the padded format the Merkle tree can't share a block with the data.
  Inode inode =
      AddUncompressedBlob(blobfs->GetBlockSize() * 2 - digest::kSha256Length * 2, *blobfs);
  EXPECT_FALSE(inode.IsCompressed());
  EXPECT_EQ(inode.block_count, 3u);

  // Check that the blob can be read back and verified.
  BlobfsChecker checker(std::move(blobfs), {.repair = false});
  EXPECT_EQ(checker.Check(), ZX_OK);
}

TEST(BlobfsHostTest, WriteBlobWithCompactFormatAndSharedBlockIsCorrect) {
  auto blobfs = CreateBlobfs(/*block_count=*/500,
                             {.blob_layout_format = BlobLayoutFormat::kCompactMerkleTreeAtEnd});
  ASSERT_TRUE(blobfs != nullptr);

  // In the compact format the Merkle tree will fit perfectly into the end of the data.
  ASSERT_EQ(blobfs->GetBlockSize(), digest::kDefaultNodeSize);
  Inode inode =
      AddUncompressedBlob(blobfs->GetBlockSize() * 2 - digest::kSha256Length * 2, *blobfs);
  EXPECT_FALSE(inode.IsCompressed());
  EXPECT_EQ(inode.block_count, 2u);

  // Check that the blob can be read back and verified.
  BlobfsChecker checker(std::move(blobfs), {.repair = false});
  EXPECT_EQ(checker.Check(), ZX_OK);
}

TEST(BlobfsHostTest, WriteBlobWithCompactFormatAndBlockIsNotSharedIsCorrect) {
  auto blobfs = CreateBlobfs(/*block_count=*/500,
                             {.blob_layout_format = BlobLayoutFormat::kCompactMerkleTreeAtEnd});
  ASSERT_TRUE(blobfs != nullptr);

  // The Merkle tree doesn't fit in with the data.
  ASSERT_EQ(blobfs->GetBlockSize(), digest::kDefaultNodeSize);
  Inode inode = AddUncompressedBlob(blobfs->GetBlockSize() * 2 - 10, *blobfs);
  EXPECT_FALSE(inode.IsCompressed());
  EXPECT_EQ(inode.block_count, 3u);

  // Check that the blob can be read back and verified.
  BlobfsChecker checker(std::move(blobfs), {.repair = false});
  EXPECT_EQ(checker.Check(), ZX_OK);
}

TEST(BlobfsHostTest, WriteCompressedBlobWithCompactFormatAndSharedBlockIsCorrect) {
  auto blobfs = CreateBlobfs(/*block_count=*/500,
                             {.blob_layout_format = BlobLayoutFormat::kCompactMerkleTreeAtEnd});
  ASSERT_TRUE(blobfs != nullptr);

  // The blob is compressed to well under 1 block which leaves plenty of room for the Merkle tree.
  Inode inode = AddCompressedBlob(blobfs->GetBlockSize() * 2, *blobfs);
  EXPECT_TRUE(inode.IsCompressed());
  EXPECT_EQ(inode.block_count, 1u);

  // Check that the blob can be read back and verified.
  BlobfsChecker checker(std::move(blobfs), {.repair = false});
  EXPECT_EQ(checker.Check(), ZX_OK);
}

TEST(BlobfsHostTest, WriteCompressedBlobWithPaddedFormatIsCorrect) {
  auto blobfs = CreateBlobfs(/*block_count=*/500,
                             {.blob_layout_format = BlobLayoutFormat::kPaddedMerkleTreeAtStart});
  ASSERT_TRUE(blobfs != nullptr);

  // The Merkle tree requires 1 block and the blob is compressed to under 1 block.
  Inode inode = AddCompressedBlob(blobfs->GetBlockSize() * 2, *blobfs);
  EXPECT_TRUE(inode.IsCompressed());
  EXPECT_EQ(inode.block_count, 2u);

  // Check that the blob can be read back and verified.
  BlobfsChecker checker(std::move(blobfs), {.repair = false});
  EXPECT_EQ(checker.Check(), ZX_OK);
}

TEST(BlobfsHostTest, WriteEmptyBlobWithCompactFormatIsCorrect) {
  auto blobfs = CreateBlobfs(/*block_count=*/500,
                             {.blob_layout_format = BlobLayoutFormat::kCompactMerkleTreeAtEnd});
  ASSERT_TRUE(blobfs != nullptr);

  Inode inode = AddUncompressedBlob(/*data_size=*/0, *blobfs);
  EXPECT_EQ(inode.block_count, 0u);

  // Check that the blob can be read back and verified.
  BlobfsChecker checker(std::move(blobfs), {.repair = false});
  EXPECT_EQ(checker.Check(), ZX_OK);
}

void CheckBlobContents(File& blob, fbl::Span<const uint8_t> contents) {
  std::vector<uint8_t> buffer(kBlobfsBlockSize);

  int read_result = 0;
  int read_bytes = 0;
  lseek(blob.fd(), 0, SEEK_SET);
  while ((read_result = read(blob.fd(), buffer.data(), buffer.size())) >= 0) {
    ASSERT_LE(static_cast<unsigned int>(read_bytes + read_result), contents.size());
    ASSERT_TRUE(memcmp(contents.data() + read_bytes, buffer.data(), read_result) == 0);
    read_bytes += read_result;
    if (read_result == 0) {
      break;
    }
  }
  ASSERT_EQ(read_result, 0);
  ASSERT_EQ(static_cast<unsigned int>(read_bytes), contents.size());
}

TEST(BlobfsHostTest, VisitBlobsVisitsAllBlobsAndProvidesTheCorrectContents) {
  auto blobfs = CreateBlobfs(/*block_count=*/500,
                             {.blob_layout_format = BlobLayoutFormat::kCompactMerkleTreeAtEnd});
  ASSERT_TRUE(blobfs != nullptr);

  unsigned int seed = testing::UnitTest::GetInstance()->random_seed();
  int blob_count = 32;
  std::vector<std::unique_ptr<File>> blobs;
  std::vector<MerkleInfo> blob_info;

  for (int i = 0; i < blob_count; ++i) {
    // 1-3 blocks and random tail(empty tail is acceptable too).
    size_t data_size = (i % 3 + 1) * kBlobfsBlockSize + (rand_r(&seed) % kBlobfsBlockSize);
    blobs.push_back(std::make_unique<File>(tmpfile()));
    blob_info.push_back({});

    InitBlob(data_size, *blobfs, *blobs.back(), blob_info.back(), &seed);
  }

  auto get_blob_index_by_digest =
      [&](fbl::Span<const uint8_t> merkle_root_hash) -> std::optional<int> {
    int i = 0;
    for (auto& info : blob_info) {
      if (info.digest.Equals(merkle_root_hash.data(), merkle_root_hash.size())) {
        return i;
      }
      ++i;
    }
    return std::nullopt;
  };

  int visited_blob_count = 0;
  auto visit_result =
      blobfs->VisitBlobs([&](Blobfs::BlobView blob_view) -> fit::result<void, std::string> {
        auto blob_index = get_blob_index_by_digest(blob_view.merkle_hash);
        if (!blob_index.has_value()) {
          return fit::error("Blob not found!");
        }
        CheckBlobContents(*blobs[blob_index.value()], blob_view.blob_contents);
        visited_blob_count++;
        return fit::ok();
      });
  ASSERT_TRUE(visit_result.is_ok()) << visit_result.error();
  ASSERT_EQ(visited_blob_count, blob_count);

  // Check that the blob can be read back and verified.
  BlobfsChecker checker(std::move(blobfs), {.repair = false});
  EXPECT_EQ(checker.Check(), ZX_OK);
}

TEST(BlobfsHostTest, VisitBlobsForwardsVisitorErrors) {
  auto blobfs = CreateBlobfs(/*block_count=*/500,
                             {.blob_layout_format = BlobLayoutFormat::kCompactMerkleTreeAtEnd});
  ASSERT_TRUE(blobfs != nullptr);

  // One blob to visit at least.
  AddUncompressedBlob(/*data_size=*/0, *blobfs);

  auto res = blobfs->VisitBlobs([](auto view) { return fit::error("1234"); });

  ASSERT_TRUE(res.is_error());
  ASSERT_TRUE(res.error().find("1234") != std::string::npos);
}

std::vector<uint8_t> ReadFileContents(int fd) {
  std::vector<uint8_t> data(1);
  std::vector<uint8_t> buffer(kBlobfsBlockSize);
  int read_bytes = 0;
  int read_result = 0;
  while ((read_result = read(fd, buffer.data(), buffer.size())) > 0) {
    data.resize(read_bytes + read_result);
    memcpy(&data[read_bytes], buffer.data(), read_result);
    read_bytes += read_result;
    if (read_result == 0) {
      return data;
    }
  }
  return data;
}

TEST(BlobfsHostTest, ExportBlobsCreatesBlobsWithTheCorrectContentAndName) {
  auto blobfs = CreateBlobfs(/*block_count=*/500,
                             {.blob_layout_format = BlobLayoutFormat::kCompactMerkleTreeAtEnd});
  ASSERT_TRUE(blobfs != nullptr);

  unsigned int seed = testing::UnitTest::GetInstance()->random_seed();
  int blob_count = 20;
  std::vector<std::unique_ptr<File>> blobs;
  std::vector<MerkleInfo> blob_info;

  auto find_blob_index_by_name = [&](const char* name) -> std::optional<int> {
    std::string target(name);
    for (int i = 0; i < blob_count; ++i) {
      auto& info = blob_info[i];
      auto blob_name = std::string(info.digest.ToString().c_str(), info.digest.ToString().length());
      if (target == blob_name) {
        return i;
      }
    }
    return std::nullopt;
  };

  for (int i = 0; i < blob_count; ++i) {
    // 1-3 blocks and random tail(empty tail is acceptable too).
    size_t data_size = (i % 3 + 1) * kBlobfsBlockSize + (rand_r(&seed) % kBlobfsBlockSize);
    blobs.push_back(std::make_unique<File>(tmpfile()));
    blob_info.push_back({});

    InitBlob(data_size, *blobfs, *blobs.back(), blob_info.back(), &seed);
  }

  // Create a temporal output dir.
  std::string tmp_dir = "blob_output_test.XXXXXX";
  char* dir_name = mkdtemp(tmp_dir.data());
  ASSERT_NE(dir_name, nullptr);

  fbl::unique_fd output_dir(open(dir_name, O_DIRECTORY));
  ASSERT_TRUE(output_dir.is_valid());

  auto export_result = ExportBlobs(output_dir.get(), *blobfs);
  ASSERT_TRUE(export_result.is_ok()) << export_result.error();

  // Iterate and validate each entry.
  DIR* output = opendir(dir_name);
  ASSERT_NE(output, nullptr);

  dirent* entry = nullptr;
  while ((entry = readdir(output)) != nullptr) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }
    fbl::unique_fd blob_fd(openat(output_dir.get(), entry->d_name, O_RDONLY));
    ASSERT_TRUE(blob_fd.is_valid());
    auto index_or = find_blob_index_by_name(entry->d_name);
    ASSERT_TRUE(index_or.has_value());
    auto contents = ReadFileContents(blob_fd.get());
    CheckBlobContents(*blobs[index_or.value()], contents);
  }
  closedir(output);
}

}  // namespace
}  // namespace blobfs
