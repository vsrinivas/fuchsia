// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.device/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>

#include <random>
#include <thread>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/storage/fs_test/fs_test.h"
#include "src/storage/fs_test/test_filesystem.h"

namespace fs_test {
namespace {

namespace device = fuchsia_device;

class CorruptTest : public testing::Test,
                    public testing::WithParamInterface<TestFilesystemOptions> {};

TEST_P(CorruptTest, CorruptTest) {
  // 768 blocks containing 64 pages of 4 KiB with 8 bytes OOB
  constexpr int kSize = 768 * 64 * (4096 + 8);

  for (int pass = 0; pass < 2; ++pass) {
    fzl::OwnedVmoMapper vmo;
    ASSERT_EQ(vmo.CreateAndMap(kSize, "corrupt-test-vmo"), ZX_OK);
    memset(vmo.start(), 0xff, kSize);

    TestFilesystemOptions options = GetParam();
    options.device_block_size = 8192;
    options.device_block_count = 0;  // Use VMO size.
    options.use_ram_nand = true;
    options.vmo = zx::unowned_vmo(vmo.vmo());
    if (options.use_fvm) {
      // Create a dummy FVM partition that shifts the location of the minfs partition such that the
      // offsets being used will hit the second half of the FTL's 8 KiB map pages.
      options.dummy_fvm_partition_size = 8'388'608;
    } else {
      options.use_fvm = true;
      options.fvm_slice_size = 32'768;
      options.initial_fvm_slice_count = 5120;  // Leaves 32 MiB for FVM & FTL metadata.
    }
    std::random_device random;

    if (pass == 0) {
      // In this first pass, a write failure is closely targeted such that the failure is more
      // likely to occur just as the FTL is writing the first page of a new map block.  If things
      // change with the write pattern for minfs, then this range might not be right, so on the
      // second pass, we target a much wider range.
      std::uniform_int_distribution distribution(1325, 1400);

      // Deliberately fail after an odd number so that we always fail half-way through an 8 KiB
      // write.
      options.fail_after = distribution(random) | 1;
    } else {
      // On the second pass, we use a wider random range in case a change in the system means that
      // the first pass no longer targets weak spots.
      std::uniform_int_distribution distribution(1300, 2300);
      options.fail_after = distribution(random);
    }

    {
      auto fs_or = TestFilesystem::Create(options);
      ASSERT_TRUE(fs_or.is_ok()) << fs_or.status_string();
      TestFilesystem fs = std::move(fs_or).value();

      // Loop until we encounter write failures.
      const std::string file1 = fs.mount_path() + "/file1";
      const std::string file2 = fs.mount_path() + "/file2";
      for (;;) {
        {
          fbl::unique_fd fd(open(file1.c_str(), O_RDWR | O_CREAT, 0644));
          if (!fd || write(fd.get(), "hello", 5) != 5 || ftruncate(fd.get(), 0) != 0 ||
              fsync(fd.get()) != 0 || unlink(file1.c_str()) != 0) {
            break;
          }
        }
        {
          fbl::unique_fd fd(open(file2.c_str(), O_RDWR | O_CREAT, 0644));
          if (!fd || write(fd.get(), "hello", 5) != 5 || ftruncate(fd.get(), 0) != 0 ||
              fsync(fd.get()) != 0 || unlink(file2.c_str()) != 0) {
            break;
          }
        }
      }
    }

    std::cout << "Remounting" << std::endl;
    options.fail_after = 0;
    auto fs_or = TestFilesystem::Open(options);
    ASSERT_TRUE(fs_or.is_ok()) << fs_or.status_string();
    TestFilesystem fs = std::move(fs_or).value();
    EXPECT_EQ(fs.Unmount().status_value(), ZX_OK);
    EXPECT_EQ(fs.Fsck().status_value(), ZX_OK);
  }
}

TEST_P(CorruptTest, OutOfOrderWrites) {
  constexpr int kSize = 768 * 64 * 4096;
  fzl::OwnedVmoMapper vmo;
  ASSERT_EQ(vmo.CreateAndMap(kSize, "corrupt-test-vmo"), ZX_OK);
  memset(vmo.start(), 0xff, kSize);

  TestFilesystemOptions options = GetParam();
  options.device_block_size = 8192;
  options.device_block_count = 0;  // Use VMO size.
  options.vmo = zx::unowned_vmo(vmo.vmo());
  if (options.use_fvm) {
    // Create a dummy FVM partition that shifts the location of the minfs partition such that the
    // offsets being used will hit the second half of the FTL's 8 KiB map pages.
    options.dummy_fvm_partition_size = 8'388'608;
  } else {
    options.use_fvm = true;
    options.fvm_slice_size = 32'768;
    options.initial_fvm_slice_count = 5120;  // Leaves 32 MiB for FVM & FTL metadata.
  }
  std::random_device random;
  std::uniform_int_distribution distribution(1300, 2300);
  options.fail_after = distribution(random);
  options.ram_disk_discard_random_after_last_flush = true;

  {
    auto fs_or = TestFilesystem::Create(options);
    ASSERT_TRUE(fs_or.is_ok()) << fs_or.status_string();
    TestFilesystem fs = std::move(fs_or).value();

    const std::string file1 = fs.mount_path() + "/file1";
    const std::string file2 = fs.mount_path() + "/file2";
    for (;;) {
      {
        fbl::unique_fd fd(open(file1.c_str(), O_RDWR | O_CREAT, 0644));
        if (!fd || write(fd.get(), "hello", 5) != 5 || ftruncate(fd.get(), 0) != 0 ||
            fsync(fd.get()) != 0 || unlink(file1.c_str()) != 0) {
          break;
        }
      }
      {
        fbl::unique_fd fd(open(file2.c_str(), O_RDWR | O_CREAT, 0644));
        if (!fd || write(fd.get(), "hello", 5) != 5 || ftruncate(fd.get(), 0) != 0 ||
            fsync(fd.get()) != 0 || unlink(file2.c_str()) != 0) {
          break;
        }
      }
    }

    ASSERT_EQ(fs.Unmount().status_value(), ZX_OK);
    ASSERT_EQ(fs.GetRamDisk()->Wake().status_value(), ZX_OK);
  }

  std::cout << "Remounting" << std::endl;
  options.fail_after = 0;
  auto fs_or = TestFilesystem::Open(options);
  ASSERT_TRUE(fs_or.is_ok()) << fs_or.status_string();
  TestFilesystem fs = std::move(fs_or).value();
  EXPECT_EQ(fs.Unmount().status_value(), ZX_OK);
  EXPECT_EQ(fs.Fsck().status_value(), ZX_OK);
}

INSTANTIATE_TEST_SUITE_P(
    /*no prefix*/, CorruptTest,
    testing::ValuesIn(MapAndFilterAllTestFilesystems(
        [](const TestFilesystemOptions& options) -> std::optional<TestFilesystemOptions> {
          if (options.filesystem->GetTraits().is_journaled) {
            return options;
          } else {
            return std::nullopt;
          }
        })),
    testing::PrintToStringParamName());

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(CorruptTest);

}  // namespace
}  // namespace fs_test
