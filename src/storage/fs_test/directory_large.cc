// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <unistd.h>

#include <iostream>
#include <optional>
#include <string>

#include <fbl/unique_fd.h>

#include "src/storage/fs_test/fs_test_fixture.h"

namespace fs_test {
namespace {

using DirectoryMaxTest = FilesystemTest;

// Hopefully not pushing against any 'max file length' boundaries, but large enough to fill a
// directory quickly.
constexpr int kLargePathLength = 128;

TEST_P(DirectoryMaxTest, Max) {
  // Write the maximum number of files to a directory
  const std::string dir = "dir/";
  ASSERT_EQ(mkdir(GetPath(dir).c_str(), 0777), 0);
  int i = 0;
  for (;; ++i) {
    const std::string path = GetPath(dir + std::to_string(i)) + std::string(kLargePathLength, '.');
    if (i % 100 == 0) {
      std::cerr << "Wrote " << i << " direntries" << std::endl;
    }

    fbl::unique_fd fd(open(path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0644));
    if (!fd) {
      std::cerr << "Wrote " << i << " direntries" << std::endl;
      break;
    }
  }

  EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
  EXPECT_EQ(fs().Fsck().status_value(), ZX_OK);
  EXPECT_EQ(fs().Mount().status_value(), ZX_OK);

  // Unlink all those files
  for (i -= 1; i >= 0; i--) {
    const std::string path = GetPath(dir + std::to_string(i)) + std::string(kLargePathLength, '.');
    ASSERT_EQ(unlink(path.c_str()), 0);
  }
}

INSTANTIATE_TEST_SUITE_P(
    /*no prefix*/, DirectoryMaxTest,
    testing::ValuesIn(MapAndFilterAllTestFilesystems(
        [](TestFilesystemOptions options) -> std::optional<TestFilesystemOptions> {
          // Filesystems such as memfs cannot run this test because they OOM (as expected, given
          // memory is the limiting factor).
          if (options.filesystem->GetTraits().in_memory)
            return std::nullopt;
          if (options.filesystem->GetTraits().name == "fatfs") {
            // Fatfs is slow and, other than the root directory on FAT12/16, is limited by the size
            // of the ram-disk rather than a directory size limit, so use a small ram-disk to keep
            // run-time reasonable.
            options.device_block_count = 256;
          }
          return options;
        })),
    testing::PrintToStringParamName());

}  // namespace
}  // namespace fs_test
