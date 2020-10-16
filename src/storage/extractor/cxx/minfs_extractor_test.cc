// Copyright 2020 The Fuchsia Authors. All rights reserved.
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

#include "src/storage/extractor/c/extractor.h"
#include "src/storage/extractor/cxx/extractor.h"
#include "src/storage/fs_test/fs_test.h"
#include "src/storage/fs_test/fs_test_fixture.h"
#include "src/storage/fs_test/minfs_test.h"
#include "src/storage/minfs/format.h"

namespace extractor {
namespace {

using MinfsExtractionTest = fs_test::FilesystemTest;

TEST(MinfsExtract, Extract) {
  char minfs_c_str[] = "/tmp/minfs.XXXXXX";
  ASSERT_NE(mkdtemp(minfs_c_str), nullptr);
  const std::string minfs(minfs_c_str);
  const std::string hello = minfs + "/hello";
  const std::string foo = minfs + "/foo";
  const std::string bar = minfs + "/foo/bar";
  fbl::unique_fd fd(open(hello.c_str(), O_CREAT | O_RDWR, 0777));
  ASSERT_TRUE(fd);
  ASSERT_EQ(write(fd.get(), "world", 5), 5);
  ASSERT_EQ(mkdir(foo.c_str(), 0777), 0);
  fd.reset(open(bar.c_str(), O_CREAT | O_RDWR, 0777));
  ASSERT_TRUE(fd);
  ASSERT_EQ(write(fd.get(), "bar", 3), 3);
  fd.reset();
}

// Returns valid superblock in info.
// Expects at least one valid superblock.
void GetSuperblock(int input_fd, minfs::Superblock* info) {
  ASSERT_EQ(minfs::kMinfsBlockSize,
            pread(input_fd, info, minfs::kMinfsBlockSize, minfs::kSuperblockStart));
  if (info->magic0 == minfs::kMinfsMagic0 && info->magic1 == minfs::kMinfsMagic1) {
    return;
  }
  if (minfs::kMinfsBlockSize == pread(input_fd, info, minfs::kMinfsBlockSize,
                                      minfs::kFvmSuperblockBackup * minfs::kMinfsBlockSize)) {
    if (info->magic0 == minfs::kMinfsMagic0 && info->magic1 == minfs::kMinfsMagic1) {
      return;
    }
  }
  ASSERT_EQ(minfs::kMinfsBlockSize, pread(input_fd, info, minfs::kMinfsBlockSize,
                                          minfs::kNonFvmSuperblockBackup * minfs::kMinfsBlockSize));
  ASSERT_EQ(info->magic0, minfs::kMinfsMagic0);
  ASSERT_EQ(info->magic1, minfs::kMinfsMagic1);
}

uint64_t EmptyFilesystemImageSize(const minfs::Superblock& info) {
  // Image file contains three blocks - one block for header and one block for extent cluster and
  // extents.
  constexpr uint64_t kExtractedImageBlockCount = 2;
  uint64_t block_count = kExtractedImageBlockCount;

  block_count += (2 * minfs::kSuperblockBlocks);
  block_count += minfs::NonDataBlocks(info);

  // One block for root directory.
  block_count++;

  return block_count * info.BlockSize();
}

void VerifyExtractedImage(int input_fd, uint64_t data_blocks, int output_fd) {
  minfs::Superblock info;
  GetSuperblock(input_fd, &info);

  struct stat stats;
  ASSERT_EQ(fstat(output_fd, &stats), 0);

  ssize_t expected = EmptyFilesystemImageSize(info) + (data_blocks * info.BlockSize());
  ASSERT_EQ(expected, stats.st_size);
}

fbl::unique_fd CreateAndExtract(fbl::unique_fd& input_fd, bool dump_pii) {
  char out_path[] = "/tmp/minfs-extraction.XXXXXX";
  fbl::unique_fd output_fd(mkostemp(out_path, O_RDWR | O_CREAT | O_EXCL));
  EXPECT_TRUE(output_fd);
  ExtractorOptions options = ExtractorOptions{
      .force_dump_pii = dump_pii, .add_checksum = false, .alignment = minfs::kMinfsBlockSize};
  auto extractor =
      std::move(Extractor::Create(input_fd.duplicate(), options, output_fd.duplicate()).value());
  auto status = MinfsExtract(input_fd.duplicate(), *extractor);
  EXPECT_TRUE(status.is_ok());
  EXPECT_TRUE(extractor->Write().is_ok());
  return output_fd;
}

void RunMinfsExtraction(fs_test::FilesystemTest* test, bool create_file, bool dump_pii,
                        bool corrupt_superblock = false) {
  constexpr const char* kFilename = "this_is_a_test_file.txt";
  uint64_t kDumpedBlocks = 1;
  char buffer[minfs::kMinfsBlockSize * kDumpedBlocks];
  memset(buffer, 0xf0, sizeof(buffer));
  if (create_file) {
    auto file_path = test->GetPath(kFilename);
    fbl::unique_fd test_file(open(file_path.c_str(), O_CREAT | O_RDWR));
    ASSERT_EQ(write(test_file.get(), buffer, sizeof(buffer)), static_cast<ssize_t>(sizeof(buffer)));
  }

  EXPECT_EQ(test->fs().Unmount().status_value(), ZX_OK);

  fbl::unique_fd input_fd(open(test->fs().DevicePath().value().c_str(), O_RDONLY));
  ASSERT_TRUE(input_fd);
  if (corrupt_superblock) {
    fbl::unique_fd writeable_input_fd(open(test->fs().DevicePath().value().c_str(), O_RDWR));
    ASSERT_TRUE(writeable_input_fd);
    uint8_t zero_buffer[minfs::kSuperblockBlocks * minfs::kMinfsBlockSize];
    memset(zero_buffer, 0, sizeof(zero_buffer));
    ASSERT_EQ(
        pwrite(writeable_input_fd.get(), zero_buffer, sizeof(zero_buffer), minfs::kSuperblockStart),
        static_cast<ssize_t>(sizeof(zero_buffer)));
  }
  auto output_fd = CreateAndExtract(input_fd, dump_pii);

  VerifyExtractedImage(input_fd.get(), create_file && dump_pii ? kDumpedBlocks : 0,
                       output_fd.get());

  if (!dump_pii || !create_file) {
    return;
  }

  minfs::Superblock info;
  GetSuperblock(input_fd.get(), &info);
  char read_buffer[minfs::kMinfsBlockSize * kDumpedBlocks];
  ASSERT_EQ(
      pread(output_fd.get(), read_buffer, sizeof(read_buffer), EmptyFilesystemImageSize(info)),
      static_cast<ssize_t>(sizeof(read_buffer)));
  ASSERT_EQ(memcmp(buffer, read_buffer, sizeof(read_buffer)), 0);
}

TEST_P(MinfsExtractionTest, DumpEmptyMinfs) {
  RunMinfsExtraction(this, /*create_file=*/false, /*dump_pii=*/false);
}

TEST_P(MinfsExtractionTest, NoPiiDumped) {
  RunMinfsExtraction(this, /*create_file=*/true, /*dump_pii=*/false);
}

TEST_P(MinfsExtractionTest, PiiDumped) {
  RunMinfsExtraction(this, /*create_file=*/true, /*dump_pii=*/true);
}

TEST_P(MinfsExtractionTest, CorruptedPrimarySuperblock) {
  RunMinfsExtraction(this, /*create_file=*/true, /*dump_pii=*/true,
                     /*corrupt_superblock=*/true);
}

// Test if we traverse indirect and double indirect blocks.
void LargeFileTestRunner(fs_test::FilesystemTest* test, bool dump_pii) {
  constexpr const char* kFilename = "this_is_a_test_file.txt";
  uint64_t kDumpedDataBlocks = 3;
  uint64_t dumped_metadata_blocks = 0;
  char buffer[minfs::kMinfsBlockSize];
  memset(buffer, 0xf0, sizeof(buffer));
  {
    auto file_path = test->GetPath(kFilename);
    fbl::unique_fd test_file(open(file_path.c_str(), O_CREAT | O_RDWR));
    ASSERT_EQ(write(test_file.get(), buffer, sizeof(buffer)), static_cast<ssize_t>(sizeof(buffer)));

    // Write at indirect offset
    ASSERT_EQ(pwrite(test_file.get(), buffer, sizeof(buffer), 1024 * 1024),
              static_cast<ssize_t>(sizeof(buffer)));
    dumped_metadata_blocks++;

    // Write at double indirect offset
    ASSERT_EQ(pwrite(test_file.get(), buffer, sizeof(buffer), 1024 * 1024 * 1024),
              static_cast<ssize_t>(sizeof(buffer)));
    dumped_metadata_blocks += 2;
  }

  EXPECT_EQ(test->fs().Unmount().status_value(), ZX_OK);

  fbl::unique_fd input_fd(open(test->fs().DevicePath().value().c_str(), O_RDONLY));
  ASSERT_TRUE(input_fd);

  auto output_fd = CreateAndExtract(input_fd, dump_pii);

  VerifyExtractedImage(
      input_fd.get(),
      dump_pii ? dumped_metadata_blocks + kDumpedDataBlocks : dumped_metadata_blocks,
      output_fd.get());

  minfs::Superblock info;
  GetSuperblock(input_fd.get(), &info);
  char read_buffer[minfs::kMinfsBlockSize];

  ASSERT_EQ(lseek(output_fd.get(), EmptyFilesystemImageSize(info), SEEK_SET),
            static_cast<ssize_t>(EmptyFilesystemImageSize(info)));
  // Data was dumped then first block should be a data block.
  if (dump_pii) {
    ASSERT_EQ(read(output_fd.get(), read_buffer, sizeof(read_buffer)),
              static_cast<ssize_t>(sizeof(read_buffer)));
    ASSERT_EQ(memcmp(buffer, read_buffer, sizeof(read_buffer)), 0);
  }

  // Data pointed by the indirect block.
  if (dump_pii) {
    ASSERT_EQ(read(output_fd.get(), read_buffer, sizeof(read_buffer)),
              static_cast<ssize_t>(sizeof(read_buffer)));
    ASSERT_EQ(memcmp(buffer, read_buffer, sizeof(read_buffer)), 0);
  }
  // First indirect block.
  ASSERT_EQ(read(output_fd.get(), read_buffer, sizeof(read_buffer)),
            static_cast<ssize_t>(sizeof(read_buffer)));
  ASSERT_NE(memcmp(buffer, read_buffer, sizeof(read_buffer)), 0);

  // Data pointed by double indirect -> indirect block.
  if (dump_pii) {
    ASSERT_EQ(read(output_fd.get(), read_buffer, sizeof(read_buffer)),
              static_cast<ssize_t>(sizeof(read_buffer)));
    ASSERT_EQ(memcmp(buffer, read_buffer, sizeof(read_buffer)), 0);
  }
  // Double indirect block.
  ASSERT_EQ(read(output_fd.get(), read_buffer, sizeof(read_buffer)),
            static_cast<ssize_t>(sizeof(read_buffer)));
  ASSERT_NE(memcmp(buffer, read_buffer, sizeof(read_buffer)), 0);
  // Indirect block pointed by the double indirect block.
  ASSERT_EQ(read(output_fd.get(), read_buffer, sizeof(read_buffer)),
            static_cast<ssize_t>(sizeof(read_buffer)));
  ASSERT_NE(memcmp(buffer, read_buffer, sizeof(read_buffer)), 0);
}

TEST_P(MinfsExtractionTest, LargeFileWithNoPii) { LargeFileTestRunner(this, /*dump_pii=*/false); }

TEST_P(MinfsExtractionTest, LargeFileWithPii) { LargeFileTestRunner(this, /*dump_pii=*/true); }

// Test if we traverse indirect and double indirect blocks.
void DirectoryTestRunner(fs_test::FilesystemTest* test, bool dump_pii) {
  const std::string kFilename("this_is_a_test_file.txt");
  constexpr const char* kDirectory = "this_is_a_test_directory/";
  constexpr uint8_t kDirectoryBlocks = 1;
  {
    auto directory_path = test->GetPath(kDirectory);
    ASSERT_EQ(mkdir(directory_path.c_str(), O_RDWR), 0);

    auto file_path = directory_path;
    file_path.append(kFilename);

    fbl::unique_fd test_file(open(file_path.c_str(), O_CREAT | O_RDWR));
    ASSERT_TRUE(test_file);
    fprintf(stderr, "%s\n", file_path.c_str());
  }

  EXPECT_EQ(test->fs().Unmount().status_value(), ZX_OK);

  fbl::unique_fd input_fd(open(test->fs().DevicePath().value().c_str(), O_RDONLY));
  ASSERT_TRUE(input_fd);

  auto output_fd = CreateAndExtract(input_fd, dump_pii);
  // Irrespective of dump_pii value, we should dump directory contents.
  VerifyExtractedImage(input_fd.get(), kDirectoryBlocks, output_fd.get());

  minfs::Superblock info;
  GetSuperblock(input_fd.get(), &info);
  char read_buffer[minfs::kMinfsBlockSize];

  ASSERT_EQ(lseek(output_fd.get(), EmptyFilesystemImageSize(info), SEEK_SET),
            static_cast<ssize_t>(EmptyFilesystemImageSize(info)));
  ASSERT_EQ(read(output_fd.get(), read_buffer, sizeof(read_buffer)),
            static_cast<ssize_t>(sizeof(read_buffer)));
  ASSERT_NE(std::search(std::begin(read_buffer), std::end(read_buffer), std::begin(kFilename),
                        std::end(kFilename)),
            std::end(read_buffer));
}

TEST_P(MinfsExtractionTest, DumpDirectoryWithNoPii) {
  DirectoryTestRunner(this, /*dump_pii=*/false);
}

TEST_P(MinfsExtractionTest, DumpDirectoryWithPii) { DirectoryTestRunner(this, /*dump_pii=*/true); }

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, MinfsExtractionTest,
                         testing::ValuesIn(fs_test::AllTestMinfs()),
                         testing::PrintToStringParamName());

}  // namespace

}  // namespace extractor
