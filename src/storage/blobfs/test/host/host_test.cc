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
#include <zircon/errors.h>

#include <cstring>
#include <limits>
#include <memory>
#include <optional>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/lib/chunked-compression/multithreaded-chunked-compressor.h"
#include "src/lib/digest/digest.h"
#include "src/lib/digest/node-digest.h"
#include "src/storage/blobfs/blob_layout.h"
#include "src/storage/blobfs/blobfs_checker.h"
#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/node_finder.h"

namespace blobfs {
namespace {

constexpr std::string_view kSrcFilePath = "/path/to/blob/src";

constexpr FilesystemOptions DefaultFilesystemOptions() {
  return FilesystemOptions{
      .num_inodes = 512,
  };
}

constexpr FilesystemOptions CreateFilesystemOptions(BlobLayoutFormat format) {
  auto options = DefaultFilesystemOptions();
  options.blob_layout_format = format;
  return options;
}

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

std::unique_ptr<Blobfs> CreateBlobfs(
    uint64_t block_count, const FilesystemOptions& options = DefaultFilesystemOptions()) {
  File fs_file(tmpfile());
  if (ftruncate(fs_file.fd(), static_cast<off_t>(block_count * kBlobfsBlockSize)) == -1) {
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

std::optional<Inode> FindInodeByMerkleDigest(Blobfs& blobfs, const digest::Digest& digest) {
  for (uint32_t i = 0; i < blobfs.Info().alloc_inode_count; ++i) {
    auto inode = blobfs.GetNode(i);
    ZX_ASSERT(inode.is_ok());
    if (inode == nullptr) {
      return std::nullopt;
    }
    if (!inode->header.IsAllocated() || !inode->header.IsInode()) {
      continue;
    }
    if (digest == inode->merkle_root_hash) {
      return *inode.value();
    }
  }
  return std::nullopt;
}

void FillFileWithRandomContent(File& file, size_t size, unsigned int* seed) {
  std::vector<uint8_t> file_contents(size, 0);

  for (auto& b : file_contents) {
    b = static_cast<uint8_t>(rand_r(seed) % std::numeric_limits<uint8_t>::max());
  }

  ssize_t written = 0;
  ssize_t write_result = 0;
  while ((write_result = write(file.fd(), file_contents.data() + written,
                               file_contents.size() - written)) > 0) {
    written += write_result;
  }
  ASSERT_EQ(write_result, 0);
  ASSERT_EQ(written, static_cast<ssize_t>(size));
}

std::unique_ptr<File> CreateEmptyFile(uint64_t file_size) {
  auto file = std::make_unique<File>(tmpfile());
  EXPECT_EQ(ftruncate(file->fd(), file_size), 0);
  return file;
}

std::unique_ptr<File> CreateFileWithRandomContent(uint64_t file_size, unsigned int* seed) {
  auto file = CreateEmptyFile(file_size);
  FillFileWithRandomContent(*file, file_size, seed);
  return file;
}

zx::result<BlobInfo> CreateCompressedBlob(int fd, BlobLayoutFormat blob_layout_format) {
  chunked_compression::MultithreadedChunkedCompressor compressor(/*thread_count=*/1);
  return BlobInfo::CreateCompressed(fd, blob_layout_format, kSrcFilePath, compressor);
}

// Adds an uncompressed blob of size |data_size| to |blobfs| and returns the created blob's Inode.
Inode AddUncompressedBlob(uint64_t data_size, Blobfs& blobfs) {
  unsigned int seed = testing::UnitTest::GetInstance()->random_seed();
  auto file = CreateFileWithRandomContent(data_size, &seed);
  auto blob_info =
      BlobInfo::CreateUncompressed(file->fd(), GetBlobLayoutFormat(blobfs.Info()), kSrcFilePath);
  ZX_ASSERT(blob_info.is_ok());
  EXPECT_FALSE(blob_info->IsCompressed());

  ZX_ASSERT(blobfs.AddBlob(blob_info.value()).is_ok());

  return FindInodeByMerkleDigest(blobfs, blob_info->GetDigest()).value();
}

// Adds a compressed blob with an uncompressed size of |data_size| to |blobfs| and returns the
// created blob's Inode.  The blobs data will be all zeros which will be significantly compressed.
Inode AddCompressedBlob(uint64_t data_size, Blobfs& blobfs) {
  auto file = CreateEmptyFile(data_size);
  auto blob_info = CreateCompressedBlob(file->fd(), GetBlobLayoutFormat(blobfs.Info()));
  ZX_ASSERT(blob_info.is_ok());
  // Make sure that the blob was compressed.
  EXPECT_TRUE(blob_info->IsCompressed());

  ZX_ASSERT(blobfs.AddBlob(blob_info.value()).is_ok());

  return FindInodeByMerkleDigest(blobfs, blob_info->GetDigest()).value();
}

TEST(BlobfsHostFormatTest, FormatDevice) {
  File file(tmpfile());
  EXPECT_EQ(Mkfs(file.fd(), 10000, DefaultFilesystemOptions()), 0);
}

TEST(BlobfsHostFormatTest, FormatDeviceWithExtraInodes) {
  File file(tmpfile());
  EXPECT_EQ(Mkfs(file.fd(), 10000, FilesystemOptions{.num_inodes = kBlobfsDefaultInodeCount + 1}),
            0);
}

TEST(BlobfsHostFormatTest, FormatZeroBlockDevice) {
  File file(tmpfile());
  EXPECT_EQ(Mkfs(file.fd(), 0, DefaultFilesystemOptions()), ZX_ERR_NO_SPACE);
}

TEST(BlobfsHostFormatTest, FormatTooSmallDevice) {
  File file(tmpfile());
  EXPECT_EQ(Mkfs(file.fd(), 1, DefaultFilesystemOptions()), ZX_ERR_NO_SPACE);
}

TEST(BlobfsHostFormatTest, FormatTooFewInodes) {
  File file(tmpfile());
  EXPECT_EQ(Mkfs(file.fd(), 10000 / 2, FilesystemOptions{.num_inodes = 0}), -1);
}

// This test verifies that formatting actually writes zero-filled
// blocks within the journal.
TEST(BlobfsHostFormatTest, JournalFormattedAsEmpty) {
  File file(tmpfile());
  constexpr uint64_t kBlockCount = 10000;
  EXPECT_EQ(Mkfs(file.fd(), kBlockCount, DefaultFilesystemOptions()), 0);

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
  constexpr size_t kAllZeroSize{static_cast<size_t>(12) * 1024};
  auto file = CreateEmptyFile(kAllZeroSize);

  auto blob_info =
      CreateCompressedBlob(file->fd(), BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart);
  ASSERT_TRUE(blob_info.is_ok());

  EXPECT_TRUE(blob_info->IsCompressed());
  EXPECT_LE(blob_info->GetData().size(), kAllZeroSize);
}

TEST(BlobfsHostTest, WriteBlobWithPaddedFormatIsCorrect) {
  auto blobfs =
      CreateBlobfs(/*block_count=*/500,
                   CreateFilesystemOptions(BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart));
  ASSERT_TRUE(blobfs != nullptr);

  // In the padded format the Merkle tree can't share a block with the data.
  Inode inode = AddUncompressedBlob(
      blobfs->GetBlockSize() * UINT64_C(2) - digest::kSha256Length * 2, *blobfs);
  EXPECT_FALSE(inode.IsCompressed());
  EXPECT_EQ(inode.block_count, 3u);

  // Check that the blob can be read back and verified.
  BlobfsChecker checker(blobfs.get(), {.repair = false});
  EXPECT_TRUE(checker.Check());
}

TEST(BlobfsHostTest, WriteBlobWithCompactFormatAndSharedBlockIsCorrect) {
  auto blobfs = CreateBlobfs(/*block_count=*/500,
                             CreateFilesystemOptions(BlobLayoutFormat::kCompactMerkleTreeAtEnd));
  ASSERT_TRUE(blobfs != nullptr);

  // In the compact format the Merkle tree will fit perfectly into the end of the data.
  ASSERT_EQ(blobfs->GetBlockSize(), digest::kDefaultNodeSize);
  Inode inode = AddUncompressedBlob(
      blobfs->GetBlockSize() * UINT64_C(2) - digest::kSha256Length * 2, *blobfs);
  EXPECT_FALSE(inode.IsCompressed());
  EXPECT_EQ(inode.block_count, 2u);

  // Check that the blob can be read back and verified.
  BlobfsChecker checker(blobfs.get(), {.repair = false});
  EXPECT_TRUE(checker.Check());
}

TEST(BlobfsHostTest, WriteBlobWithCompactFormatAndBlockIsNotSharedIsCorrect) {
  auto blobfs = CreateBlobfs(/*block_count=*/500,
                             CreateFilesystemOptions(BlobLayoutFormat::kCompactMerkleTreeAtEnd));
  ASSERT_TRUE(blobfs != nullptr);

  // The Merkle tree doesn't fit in with the data.
  ASSERT_EQ(blobfs->GetBlockSize(), digest::kDefaultNodeSize);
  Inode inode = AddUncompressedBlob(blobfs->GetBlockSize() * 2 - 10, *blobfs);
  EXPECT_FALSE(inode.IsCompressed());
  EXPECT_EQ(inode.block_count, 3u);

  // Check that the blob can be read back and verified.
  BlobfsChecker checker(blobfs.get(), {.repair = false});
  EXPECT_TRUE(checker.Check());
}

TEST(BlobfsHostTest, WriteCompressedBlobWithCompactFormatAndSharedBlockIsCorrect) {
  auto blobfs = CreateBlobfs(/*block_count=*/500,
                             CreateFilesystemOptions(BlobLayoutFormat::kCompactMerkleTreeAtEnd));
  ASSERT_TRUE(blobfs != nullptr);

  // The blob is compressed to well under 1 block which leaves plenty of room for the Merkle tree.
  Inode inode = AddCompressedBlob(blobfs->GetBlockSize() * UINT64_C(2), *blobfs);
  EXPECT_TRUE(inode.IsCompressed());
  EXPECT_EQ(inode.block_count, 1u);

  // Check that the blob can be read back and verified.
  BlobfsChecker checker(blobfs.get(), {.repair = false});
  EXPECT_TRUE(checker.Check());
}

TEST(BlobfsHostTest, WriteCompressedBlobWithPaddedFormatIsCorrect) {
  auto blobfs =
      CreateBlobfs(/*block_count=*/500,
                   CreateFilesystemOptions(BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart));
  ASSERT_TRUE(blobfs != nullptr);

  // The Merkle tree requires 1 block and the blob is compressed to under 1 block.
  Inode inode = AddCompressedBlob(blobfs->GetBlockSize() * UINT64_C(2), *blobfs);
  EXPECT_TRUE(inode.IsCompressed());
  EXPECT_EQ(inode.block_count, 2u);

  // Check that the blob can be read back and verified.
  BlobfsChecker checker(blobfs.get(), {.repair = false});
  EXPECT_TRUE(checker.Check());
}

TEST(BlobfsHostTest, WriteEmptyBlobWithCompactFormatIsCorrect) {
  auto blobfs = CreateBlobfs(/*block_count=*/500,
                             CreateFilesystemOptions(BlobLayoutFormat::kCompactMerkleTreeAtEnd));
  ASSERT_TRUE(blobfs != nullptr);

  Inode inode = AddUncompressedBlob(/*data_size=*/0, *blobfs);
  EXPECT_EQ(inode.block_count, 0u);

  // Check that the blob can be read back and verified.
  BlobfsChecker checker(blobfs.get(), {.repair = false});
  EXPECT_TRUE(checker.Check());
}

void CheckBlobContents(File& blob, cpp20::span<const uint8_t> contents) {
  std::vector<uint8_t> buffer(kBlobfsBlockSize);

  ssize_t read_result = 0;
  ssize_t read_bytes = 0;
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
                             CreateFilesystemOptions(BlobLayoutFormat::kCompactMerkleTreeAtEnd));
  ASSERT_TRUE(blobfs != nullptr);

  unsigned int seed = testing::UnitTest::GetInstance()->random_seed();
  int blob_count = 32;
  std::vector<std::unique_ptr<File>> blobs;
  std::vector<BlobInfo> blob_infos;

  for (int i = 0; i < blob_count; ++i) {
    // 1-3 blocks and random tail(empty tail is acceptable too).
    size_t data_size = (i % 3 + 1) * kBlobfsBlockSize + (rand_r(&seed) % kBlobfsBlockSize);
    blobs.push_back(CreateFileWithRandomContent(data_size, &seed));
    auto blob_info = BlobInfo::CreateUncompressed(
        blobs.back()->fd(), GetBlobLayoutFormat(blobfs->Info()), kSrcFilePath);
    ASSERT_TRUE(blob_info.is_ok());
    blob_infos.push_back(std::move(blob_info).value());
    ASSERT_TRUE(blobfs->AddBlob(blob_infos.back()).is_ok());
  }

  auto get_blob_index_by_digest =
      [&](cpp20::span<const uint8_t> merkle_root_hash) -> std::optional<int> {
    int i = 0;
    for (auto& blob_info : blob_infos) {
      if (blob_info.GetDigest().Equals(merkle_root_hash.data(), merkle_root_hash.size())) {
        return i;
      }
      ++i;
    }
    return std::nullopt;
  };

  int visited_blob_count = 0;
  auto visit_result =
      blobfs->VisitBlobs([&](Blobfs::BlobView blob_view) -> fpromise::result<void, std::string> {
        auto blob_index = get_blob_index_by_digest(blob_view.merkle_hash);
        if (!blob_index.has_value()) {
          return fpromise::error("Blob not found!");
        }
        CheckBlobContents(*blobs[blob_index.value()], blob_view.blob_contents);
        visited_blob_count++;
        return fpromise::ok();
      });
  ASSERT_TRUE(visit_result.is_ok()) << visit_result.error();
  ASSERT_EQ(visited_blob_count, blob_count);

  // Check that the blob can be read back and verified.
  BlobfsChecker checker(blobfs.get(), {.repair = false});
  EXPECT_TRUE(checker.Check());
}

TEST(BlobfsHostTest, VisitBlobsForwardsVisitorErrors) {
  auto blobfs = CreateBlobfs(/*block_count=*/500,
                             CreateFilesystemOptions(BlobLayoutFormat::kCompactMerkleTreeAtEnd));
  ASSERT_TRUE(blobfs != nullptr);

  // One blob to visit at least.
  AddUncompressedBlob(/*data_size=*/0, *blobfs);

  auto res = blobfs->VisitBlobs([](auto view) { return fpromise::error("1234"); });

  ASSERT_TRUE(res.is_error());
  ASSERT_TRUE(res.error().find("1234") != std::string::npos);
}

std::vector<uint8_t> ReadFileContents(int fd) {
  std::vector<uint8_t> data(1);
  std::vector<uint8_t> buffer(kBlobfsBlockSize);
  ssize_t read_bytes = 0;
  ssize_t read_result = 0;
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
                             CreateFilesystemOptions(BlobLayoutFormat::kCompactMerkleTreeAtEnd));
  ASSERT_TRUE(blobfs != nullptr);

  unsigned int seed = testing::UnitTest::GetInstance()->random_seed();
  int blob_count = 20;
  std::vector<std::unique_ptr<File>> blobs;
  std::vector<BlobInfo> blob_infos;

  auto find_blob_index_by_name = [&](const char* name) -> std::optional<int> {
    fbl::String target(name);
    for (int i = 0; i < blob_count; ++i) {
      fbl::String blob_name = blob_infos[i].GetDigest().ToString();
      if (target == blob_name) {
        return i;
      }
    }
    return std::nullopt;
  };

  for (int i = 0; i < blob_count; ++i) {
    // 1-3 blocks and random tail(empty tail is acceptable too).
    size_t data_size = (i % 3 + 1) * kBlobfsBlockSize + (rand_r(&seed) % kBlobfsBlockSize);
    blobs.push_back(CreateFileWithRandomContent(data_size, &seed));
    auto blob_info = BlobInfo::CreateUncompressed(
        blobs.back()->fd(), GetBlobLayoutFormat(blobfs->Info()), kSrcFilePath);
    ASSERT_TRUE(blob_info.is_ok());
    blob_infos.push_back(std::move(blob_info).value());
    ASSERT_TRUE(blobfs->AddBlob(blob_infos.back()).is_ok());
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

TEST(BlobfsHostTest, GetNodeWithAnInvalidNodeIndexIsAnError) {
  auto blobfs = CreateBlobfs(/*block_count=*/500);
  ASSERT_TRUE(blobfs != nullptr);
  uint32_t invalid_node_index = kMaxNodeId - 1;
  auto node = blobfs->GetNode(invalid_node_index);
  EXPECT_EQ(node.status_value(), ZX_ERR_INVALID_ARGS);
}

TEST(BlobfsHostTest, CreateBlobfsWithNullBlobPassesFsck) {
  std::unique_ptr<Blobfs> blobfs = CreateBlobfs(/*block_count=*/500);
  ASSERT_TRUE(blobfs);
  AddUncompressedBlob(/*data_size=*/0, *blobfs);
  BlobfsChecker checker(blobfs.get());
  EXPECT_TRUE(checker.Check());
}

TEST(BlobfsHostTest, BlobInfoCreateCompressedWithUncompressableFileDoesNotCompressBlob) {
  unsigned int seed = testing::UnitTest::GetInstance()->random_seed();
  auto file = CreateFileWithRandomContent(2 * kBlobfsBlockSize, &seed);
  auto blob_info = CreateCompressedBlob(file->fd(), BlobLayoutFormat::kCompactMerkleTreeAtEnd);
  ASSERT_TRUE(blob_info.is_ok());
  EXPECT_FALSE(blob_info->IsCompressed());
}

TEST(BlobfsHostTest, BlobInfoCreateCompressedWithTinyFileDoesNotCompressBlob) {
  auto file = CreateEmptyFile(kBlobfsBlockSize);
  auto blob_info = CreateCompressedBlob(file->fd(), BlobLayoutFormat::kCompactMerkleTreeAtEnd);
  ASSERT_TRUE(blob_info.is_ok());
  EXPECT_FALSE(blob_info->IsCompressed());
}

TEST(BlobfsHostTest, BlobInfoCreateCompressedWithSlightlyCompressibleFileWillCompressTheBlob) {
  // Create a 2 block file where 1 and a half blocks are not compressible.
  auto file = CreateEmptyFile(2 * kBlobfsBlockSize);
  unsigned int seed = testing::UnitTest::GetInstance()->random_seed();
  FillFileWithRandomContent(*file, kBlobfsBlockSize + kBlobfsBlockSize / 2, &seed);

  // With the padded format, compressing the half block doesn't save any blocks so the file is not
  // compressed.
  auto padded_blob_info =
      CreateCompressedBlob(file->fd(), BlobLayoutFormat::kDeprecatedPaddedMerkleTreeAtStart);
  ASSERT_TRUE(padded_blob_info.is_ok());
  EXPECT_FALSE(padded_blob_info->IsCompressed());

  // With the compact format, compressing the half block saves enough space to fit the Merkle tree
  // which saves a block so the file is compressed.
  auto compact_blob_info =
      CreateCompressedBlob(file->fd(), BlobLayoutFormat::kCompactMerkleTreeAtEnd);
  ASSERT_TRUE(compact_blob_info.is_ok());
  EXPECT_TRUE(compact_blob_info->IsCompressed());
}

TEST(BlobfsHostTest, WriteBlobThatRequiresMultipleExtentsIsCorrect) {
  constexpr uint64_t kDataBlockCount{kInlineMaxExtents * Extent::kBlockCountMax + 1};
  constexpr uint64_t kExtentCount{kInlineMaxExtents + 1};
  BlobLayoutFormat blob_layout_format = BlobLayoutFormat::kCompactMerkleTreeAtEnd;

  std::unique_ptr<Blobfs> blobfs = CreateBlobfs(
      /*block_count=*/500 + kDataBlockCount, CreateFilesystemOptions(blob_layout_format));

  // Filling a 500MB file with random data takes a long time so use an empty file instead.
  auto file = CreateEmptyFile(kDataBlockCount * kBlobfsBlockSize);
  auto blob_info = BlobInfo::CreateUncompressed(file->fd(), blob_layout_format, kSrcFilePath);
  ASSERT_TRUE(blob_info.is_ok());
  EXPECT_TRUE(blobfs->AddBlob(*blob_info).is_ok());
  Inode inode = FindInodeByMerkleDigest(*blobfs, blob_info->GetDigest()).value();

  EXPECT_EQ(inode.extent_count, kExtentCount);

  auto extent_container = blobfs->GetNode(inode.header.next_node);
  ASSERT_TRUE(extent_container.is_ok());
  ASSERT_TRUE(extent_container->header.IsAllocated());
  ASSERT_TRUE(extent_container->header.IsExtentContainer());
  EXPECT_EQ(extent_container->AsExtentContainer()->extent_count, 1);

  BlobfsChecker checker(blobfs.get());
  EXPECT_TRUE(checker.Check());
}

}  // namespace
}  // namespace blobfs
