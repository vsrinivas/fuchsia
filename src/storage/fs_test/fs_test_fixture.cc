// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fs_test/fs_test_fixture.h"

#include <zircon/errors.h>

#include <gtest/gtest-spi.h>

namespace fs_test {

BaseFilesystemTest::~BaseFilesystemTest() {
  if (fs_.is_mounted()) {
    EXPECT_EQ(fs_.Unmount().status_value(), ZX_OK);
  }
  EXPECT_EQ(fs_.Fsck().status_value(), ZX_OK);
}

void BaseFilesystemTest::RunSimulatedPowerCutTest(const PowerCutOptions& options,
                                                  std::function<void()> test_function) {
  ASSERT_FALSE(fs().options().use_ram_nand);                   // This only works with ram-disks.
  ASSERT_EQ(fs().GetRamDisk()->Wake().status_value(), ZX_OK);  // This resets counts.

  // Make sure the test function runs without any failures.
  ASSERT_NO_FATAL_FAILURE(test_function());

  ramdisk_block_write_counts_t counts;
  ASSERT_EQ(ramdisk_get_block_counts(fs().GetRamDisk()->client(), &counts), ZX_OK);

  std::cout << "Total block count: " << counts.received << std::endl;

  // Now repeatedly stop writes after a certain block number.
  for (uint64_t block_cut = 1; block_cut < counts.received; block_cut += options.stride) {
    ASSERT_EQ(fs().GetRamDisk()->SleepAfter(block_cut).status_value(), ZX_OK);
    {
      // Ignore any test failures whilst we are doing this.
      testing::TestPartResultArray result;
      testing::ScopedFakeTestPartResultReporter reporter(
          testing::ScopedFakeTestPartResultReporter::InterceptMode::INTERCEPT_ALL_THREADS, &result);
      test_function();
    }
    ASSERT_EQ(fs().Unmount().status_value(), ZX_OK);
    ASSERT_EQ(fs().GetRamDisk()->Wake().status_value(), ZX_OK);
    ASSERT_EQ(fs().Fsck().status_value(), ZX_OK);
    if (options.reformat) {
      ASSERT_EQ(fs().Format().status_value(), ZX_OK);
    }
    ASSERT_EQ(fs().Mount().status_value(), ZX_OK);
  }
}

}  // namespace fs_test
