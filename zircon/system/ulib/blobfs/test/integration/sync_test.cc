// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fdio.h>

#include <blobfs/mkfs.h>
#include <blobfs/mount.h>
#include <block-client/cpp/block-device.h>
#include <block-client/cpp/fake-device.h>
#include <zxtest/zxtest.h>

#include "blobfs.h"
#include "fdio_test.h"
#include "nand_test.h"
#include "runner.h"
#include "test/blob_utils.h"

namespace blobfs {

namespace {

using SyncFdioTest = FdioTest;
using SyncNandTest = NandTest;

uint64_t GetSucceededFlushCalls(block_client::FakeBlockDevice* device) {
  fuchsia_hardware_block_BlockStats stats;
  device->GetStats(true, &stats);
  return stats.flush.success.total_calls;
}

}  // namespace

// Verifies that fdio "fsync" calls actually sync blobfs files to the block device and verifies
// behavior for different lifecycles of creating a file.
TEST_F(SyncFdioTest, Sync) {
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FAILURES(GenerateRandomBlob("", 64, &info));

  memmove(info->path, info->path + 1, strlen(info->path));  // Remove leading slash.
  int file = openat(root_fd(), info->path, O_RDWR | O_CREAT);
  EXPECT_TRUE(file >= 1);

  // We have not written any data to the file. Blobfs requires the file data to be written so the
  // name is the hash of the contents.
  EXPECT_EQ(-1, fsync(file));

  // Write the contents. The file must be truncated before writing to declare its size.
  EXPECT_EQ(0, ftruncate(file, info->size_data));
  EXPECT_EQ(info->size_data, write(file, info->data.get(), info->size_data));

  // Sync the file. This will block until woken up by the file_wake_thread.
  EXPECT_EQ(0, fsync(file));

  // fsync on a file will flush the writes to the block device but not actually flush the block
  // device itself.
  fuchsia_hardware_block_BlockStats stats;
  block_device()->GetStats(true, &stats);
  EXPECT_LE(1u, stats.write.success.total_calls);
  EXPECT_EQ(0u, stats.flush.success.total_calls);

  // Sync the root directory. Syncing a directory will force the block device to flush.
  EXPECT_EQ(0, fsync(root_fd()));
  EXPECT_EQ(1u, GetSucceededFlushCalls(block_device()));
}

// Verifies that fdio "sync" actually flushes a NAND device. This tests the fdio, blobfs, block
// device, and FTL layers.
TEST_F(SyncNandTest, Sync) {
  // Make a VMO to give to the RAM-NAND.
  const size_t vmo_size = Connection::GetVMOSize();
  zx::vmo initial_vmo;
  ASSERT_OK(zx::vmo::create(vmo_size, 0, &initial_vmo));

  // NAND VMOs must be prepopulated with 0xff.
  zx_vaddr_t vmar_address = 0;
  ASSERT_OK(zx::vmar::root_self()->map(0, initial_vmo, 0, vmo_size,
                                       ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &vmar_address));
  char* initial_vmo_data = reinterpret_cast<char*>(vmar_address);
  std::fill(initial_vmo_data, &initial_vmo_data[vmo_size], 0xff);

  // Create a second VMO for later use.
  zx::vmo second_vmo;
  ASSERT_OK(zx::vmo::create(vmo_size, 0, &second_vmo));
  ASSERT_OK(zx::vmar::root_self()->map(0, second_vmo, 0, vmo_size,
                                       ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &vmar_address));
  char* second_vmo_data = reinterpret_cast<char*>(vmar_address);

  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FAILURES(GenerateRandomBlob("", 64, &info));

  {
    Connection initial_connection("/initial/dev", std::move(initial_vmo), true);

    memmove(info->path, info->path + 1, strlen(info->path));  // Remove leading slash.
    fbl::unique_fd file(openat(initial_connection.root_fd(), info->path, O_RDWR | O_CREAT));
    ASSERT_TRUE(file.is_valid());

    // Write the contents. The file must be truncated before writing to declare its size.
    ASSERT_EQ(0, ftruncate(file.get(), info->size_data));
    ASSERT_EQ(info->size_data, write(file.get(), info->data.get(), info->size_data));

    // This should block until the sync is complete. fsync-ing the root FD is required to flush
    // everything.
    ASSERT_EQ(0, fsync(file.get()));
    ASSERT_EQ(0, fsync(initial_connection.root_fd()));

    // Without closing the file or tearing down the existing connection (which may add extra
    // flushes, etc.), create a snapshot of the current memory in the second VMO. This will emulate
    // a power cycle
    memcpy(second_vmo_data, initial_vmo_data, vmo_size);
  }

  // New connection with a completely new NAND controller reading the same memory.
  //
  // This call may fail if the above fsync on the root directory is not successful because the
  // device will have garbage data in it.
  Connection second_connection("/second/dev", std::move(second_vmo), false);

  // The blob file should exist.
  fbl::unique_fd file(openat(second_connection.root_fd(), info->path, O_RDONLY));
  ASSERT_TRUE(file.is_valid());

  // The contents should be exactly what we wrote.
  std::unique_ptr<char[]> read_data = std::make_unique<char[]>(info->size_data);
  ASSERT_EQ(info->size_data, read(file.get(), read_data.get(), info->size_data));
  EXPECT_BYTES_EQ(info->data.get(), &read_data[0], info->size_data, "mismatch");
}

}  // namespace blobfs
