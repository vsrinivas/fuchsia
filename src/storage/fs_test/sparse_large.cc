// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <unistd.h>

#include <vector>

#include <fbl/unique_fd.h>

#include "src/storage/fs_test/fs_test_fixture.h"
#include "src/storage/minfs/format.h"

namespace fs_test {
namespace {

using SparseAllocationTest = FilesystemTest;

TEST_P(SparseAllocationTest, CheckSparseFileOccupyingMultipleBitmapBlocks) {
  const std::string sparse_file = GetPath("sparse_file");
  fbl::unique_fd sparse_fd(open(sparse_file.c_str(), O_RDWR | O_CREAT, 0644));
  ASSERT_TRUE(sparse_fd);

  std::vector<uint8_t> data(minfs::kMinfsBlockSize * minfs::kMinfsBlockBits, 0xaa);

  // Create a file that owns blocks in |kBitmapBlocks| different bitmap blocks.
  constexpr uint32_t kBitmapBlocks = 4;
  for (uint32_t j = 0; j < kBitmapBlocks; j++) {
    // Write one block to the "sparse" file.
    ASSERT_EQ(write(sparse_fd.get(), data.data(), minfs::kMinfsBlockSize),
              static_cast<ssize_t>(minfs::kMinfsBlockSize));

    const std::string filename = GetPath("file_" + std::to_string(j));
    fbl::unique_fd fd(open(filename.c_str(), O_RDWR | O_CREAT, 0644));
    ASSERT_TRUE(fd);

    // Write enough blocks to another file to use up the remainder of a bitmap block.
    ASSERT_EQ(write(fd.get(), data.data(), data.size()), static_cast<ssize_t>(data.size()));
  }

  ASSERT_EQ(close(sparse_fd.release()), 0);
  ASSERT_EQ(unlink(sparse_file.c_str()), 0);
}

std::vector<TestFilesystemOptions> AllTestFilesystemsWithCustomDisk() {
  std::vector<TestFilesystemOptions> filesystems;
  for (TestFilesystemOptions options : AllTestFilesystems()) {
    // Fatfs doesn't support sparse files, is slow, and this test doesn't test more than other
    // tests, so skip it.
    if (options.filesystem->GetTraits().name != "fatfs") {
      options.device_block_count = 1LLU << 24;
      options.device_block_size = 1LLU << 9;
      options.fvm_slice_size = 1LLU << 23;
      options.zero_fill = true;
      filesystems.push_back(options);
    }
  }
  return filesystems;
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, SparseAllocationTest,
                         testing::ValuesIn(AllTestFilesystemsWithCustomDisk()),
                         testing::PrintToStringParamName());

}  // namespace
}  // namespace fs_test
