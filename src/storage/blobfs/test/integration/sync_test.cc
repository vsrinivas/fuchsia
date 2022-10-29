// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fdio.h>

#include <gtest/gtest.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/storage/blobfs/test/blob_utils.h"
#include "src/storage/blobfs/test/integration/blobfs_fixtures.h"
#include "src/storage/blobfs/test/integration/fdio_test.h"

namespace blobfs {
namespace {

using SyncFdioTest = FdioTest;

uint64_t GetSucceededFlushCalls(block_client::FakeBlockDevice* device) {
  fuchsia_hardware_block::wire::BlockStats stats;
  device->GetStats(true, &stats);
  return stats.flush.success.total_calls;
}

}  // namespace

// Verifies that fdio "fsync" calls actually sync blobfs files to the block device and verifies
// behavior for different lifecycles of creating a file.
TEST_F(SyncFdioTest, Sync) {
  std::unique_ptr<BlobInfo> info = GenerateRandomBlob("", 64);

  memmove(info->path, info->path + 1, strlen(info->path));  // Remove leading slash.
  int file = openat(root_fd(), info->path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
  EXPECT_TRUE(file >= 1);

  // We have not written any data to the file. Blobfs requires the file data to be written so the
  // name is the hash of the contents.
  EXPECT_EQ(-1, fsync(file));

  // Write the contents. The file must be truncated before writing to declare its size.
  EXPECT_EQ(0, ftruncate(file, info->size_data));
  EXPECT_EQ(write(file, info->data.get(), info->size_data), static_cast<ssize_t>(info->size_data));

  // Sync the file. This will block until woken up by the file_wake_thread.
  EXPECT_EQ(0, fsync(file));

  // fsync on a file will flush the journal but won't trigger a flush to the device above and beyond
  // those required to flush the journal.  This might change, but presently, flushing the journal
  // will trigger a flush after writing data, but before writing to the journal, another one after
  // between writing to the journal and writing to the final metadata location, and then another one
  // prior to writing a new info-block, so we should see 3 flush calls plus a flush that's triggered
  // when we format, so 4 in total.
  fuchsia_hardware_block::wire::BlockStats stats;
  block_device()->GetStats(true, &stats);
  EXPECT_LE(1u, stats.write.success.total_calls);
  EXPECT_EQ(4u, stats.flush.success.total_calls);

  // Sync the root directory. Syncing a directory will force the block device to flush.
  EXPECT_EQ(0, fsync(root_fd()));
  EXPECT_EQ(1u, GetSucceededFlushCalls(block_device()));
}

// Verifies that fdio "sync" actually flushes a NAND device. This tests the fdio, blobfs, block
// device, and FTL layers.
TEST(SyncNandTest, Sync) {
  // Make a VMO to give to the RAM-NAND.
  constexpr size_t kVmoSize{100 * static_cast<size_t>(4096 + 8) * 64};
  fzl::OwnedVmoMapper vmo;
  ASSERT_EQ(vmo.CreateAndMap(kVmoSize, "vmo"), ZX_OK);
  memset(vmo.start(), 0xff, kVmoSize);

  auto options = BlobfsWithFvmTestParam();
  options.use_ram_nand = true;
  options.vmo = vmo.vmo().borrow();
  options.device_block_count = 0;  // Uses VMO size.
  options.device_block_size = 8192;

  std::unique_ptr<BlobInfo> info = GenerateRandomBlob("", 64);

  memmove(info->path, info->path + 1, strlen(info->path));  // Remove leading slash.
  auto snapshot = std::make_unique<uint8_t[]>(kVmoSize);

  {
    auto fs_or = fs_test::TestFilesystem::Create(options);
    ASSERT_TRUE(fs_or.is_ok()) << "Unable to create file system: " << fs_or.status_string();
    auto fs = std::move(fs_or).value();

    fbl::unique_fd root_fd(open(fs.mount_path().c_str(), O_DIRECTORY));
    fbl::unique_fd file(openat(root_fd.get(), info->path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR));
    ASSERT_TRUE(file.is_valid());

    // Write the contents. The file must be truncated before writing to declare its size.
    ASSERT_EQ(ftruncate(file.get(), info->size_data), 0);
    ASSERT_EQ(write(file.get(), info->data.get(), info->size_data),
              static_cast<ssize_t>(info->size_data));

    // This should block until the sync is complete. fsync-ing the root FD is required to flush
    // everything.
    ASSERT_EQ(fsync(file.get()), 0);
    ASSERT_EQ(fsync(root_fd.get()), 0);

    // Without closing the file or tearing down the existing connection (which may add extra
    // flushes, etc.), create a snapshot of the current. This will emulate a power cycle.
    memcpy(snapshot.get(), vmo.start(), kVmoSize);
  }

  // Restore snapshot and remount.
  memcpy(vmo.start(), snapshot.get(), kVmoSize);
  auto fs_or = fs_test::TestFilesystem::Open(options);
  ASSERT_TRUE(fs_or.is_ok()) << "Unable to open file system: " << fs_or.status_string();
  auto fs = std::move(fs_or).value();

  // The blob file should exist.
  fbl::unique_fd root_fd(open(fs.mount_path().c_str(), O_DIRECTORY));
  fbl::unique_fd file(openat(root_fd.get(), info->path, O_RDONLY));
  ASSERT_TRUE(file.is_valid());

  // The contents should be exactly what we wrote.
  std::unique_ptr<char[]> read_data = std::make_unique<char[]>(info->size_data);
  ASSERT_EQ(read(file.get(), read_data.get(), info->size_data),
            static_cast<ssize_t>(info->size_data));
  EXPECT_EQ(memcmp(info->data.get(), read_data.get(), info->size_data), 0);
}

}  // namespace blobfs
