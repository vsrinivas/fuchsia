// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/zx/channel.h>

#include <random>
#include <thread>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/storage/fs_test/fs_test.h"

namespace fs_test {
namespace {

namespace device = ::llcpp::fuchsia::device;

TEST(CorruptTest, CorruptTest) {
  // 768 blocks containing 64 pages of 4 KiB with 8 bytes OOB
  constexpr int kSize = 768 * 64 * (4096 + 8);

  fzl::OwnedVmoMapper vmo;
  ASSERT_EQ(vmo.CreateAndMap(kSize, "corrupt-test-vmo"), ZX_OK);
  memset(vmo.start(), 0xff, kSize);

  TestFilesystemOptions options = TestFilesystemOptions::DefaultMinfs();
  options.device_block_size = 8192;
  options.device_block_count = 0;  // Use VMO size.
  options.use_ram_nand = true;
  options.ram_nand_vmo = zx::unowned_vmo(vmo.vmo());

  // In one thread, repeatedly create a file, write to it, sync and then delete two files.  Then,
  // some random amount of time later, tear down the Ram Nand driver, then rebind and fsck.  The
  // journal should ensure the file system remains consistent.
  {
    auto fs_or = TestFilesystem::Create(options);
    ASSERT_TRUE(fs_or.is_ok()) << fs_or.status_string();
    TestFilesystem fs = std::move(fs_or).value();

    const std::string file1 = fs.mount_path() + "/file1";
    const std::string file2 = fs.mount_path() + "/file2";
    std::thread thread([&]() {
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
    });

    std::random_device random;
    std::uniform_int_distribution distribution(0, 20000);
    const int usec = distribution(random);
    std::cerr << "sleeping for " << usec << "us" << std::endl;
    zx_nanosleep(zx_deadline_after(zx::usec(usec).get()));

    // Unbind the NAND driver.
    zx::channel local, remote;
    ASSERT_EQ(zx::channel::create(0, &local, &remote), ZX_OK);
    ASSERT_EQ(fdio_service_connect(fs.GetRamNand()->path(), remote.release()), ZX_OK);
    auto resp = device::Controller::Call::ScheduleUnbind(local.borrow());
    ASSERT_EQ(resp.status(), ZX_OK);
    ASSERT_FALSE(resp->result.is_err());

    thread.join();
  }

  auto fs_or = TestFilesystem::Open(options);
  ASSERT_TRUE(fs_or.is_ok()) << fs_or.status_string();
  TestFilesystem fs = std::move(fs_or).value();
  EXPECT_EQ(fs.Unmount().status_value(), ZX_OK);
  EXPECT_EQ(fs.Fsck().status_value(), ZX_OK);
}

}  // namespace
}  // namespace fs_test
