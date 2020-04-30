// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <blobfs/mkfs.h>
#include <blobfs/mount.h>
#include <block-client/cpp/fake-device.h>
#include <zxtest/zxtest.h>

#include "blobfs.h"
#include "fdio_test.h"
#include "runner.h"
#include "test/blob_utils.h"

namespace blobfs {

namespace {

using SyncTest = FdioTest;

uint64_t GetSucceededFlushCalls(block_client::FakeBlockDevice* device) {
  fuchsia_hardware_block_BlockStats stats;
  device->GetStats(true, &stats);
  return stats.flush.success.total_calls;
}

}  // namespace

// Verifies that fdio "fsync" calls actually sync blobfs files to the block device.
TEST_F(SyncTest, Sync) {
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

}  // namespace blobfs
