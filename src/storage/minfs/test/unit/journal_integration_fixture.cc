// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/test/unit/journal_integration_fixture.h"

#include <lib/sync/completion.h>

namespace minfs {
namespace {

using ::block_client::FakeFVMBlockDevice;

// Helper for conversion from "Bcache" to "FakeFVMBlockDevice".
void TakeDeviceFromBcache(std::unique_ptr<Bcache> bcache,
                          std::unique_ptr<block_client::FakeFVMBlockDevice>* out) {
  std::unique_ptr<block_client::BlockDevice> block_device = Bcache::Destroy(std::move(bcache));
  out->reset(reinterpret_cast<block_client::FakeFVMBlockDevice*>(block_device.release()));
}

// Helper for conversion from "Minfs" to "FakeFVMBlockDevice".
void TakeDeviceFromMinfs(std::unique_ptr<Runner> minfs,
                         std::unique_ptr<block_client::FakeFVMBlockDevice>* out) {
  std::unique_ptr<Bcache> bcache = Runner::Destroy(std::move(minfs));
  TakeDeviceFromBcache(std::move(bcache), out);
}

}  // namespace

JournalIntegrationFixture::JournalIntegrationFixture()
    : vfs_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

void JournalIntegrationFixture::SetUp() {
  auto device =
      std::make_unique<FakeFVMBlockDevice>(kBlockCount, kBlockSize, kSliceSize, kSliceCount);
  ASSERT_NO_FATAL_FAILURE(CountWritesToPerformOperation(&device));
}

std::unique_ptr<Bcache> JournalIntegrationFixture::CutOffDevice(uint64_t allowed_blocks) {
  auto device =
      std::make_unique<FakeFVMBlockDevice>(kBlockCount, kBlockSize, kSliceSize, kSliceCount);
  // Attempt to "cut-off" the operation partway by reducing the number of writes.
  PerformOperationWithTransactionLimit(allowed_blocks, &device);
  auto bcache = Bcache::Create(std::move(device), kBlockCount);
  EXPECT_TRUE(bcache.is_ok());
  return *std::move(bcache);
}

void JournalIntegrationFixture::RecordWriteCount(Minfs& fs) {
  sync_completion_t completion;
  fs.Sync([&completion](zx_status_t status) { sync_completion_signal(&completion); });
  ASSERT_EQ(sync_completion_wait(&completion, zx::duration::infinite().get()), ZX_OK);
  write_count_ =
      static_cast<FakeFVMBlockDevice*>(fs.GetMutableBcache()->device())->GetWriteBlockCount();
}

void JournalIntegrationFixture::CountWritesToPerformOperation(
    std::unique_ptr<FakeFVMBlockDevice>* in_out_device) {
  auto device = std::move(*in_out_device);
  auto bcache = Bcache::Create(std::move(device), kBlockCount);
  ASSERT_TRUE(bcache.is_ok());

  ASSERT_TRUE(Mkfs(bcache.value().get()).is_ok());
  // After formatting the device, count the number of blocks issued to the underlying device.
  TakeDeviceFromBcache(*std::move(bcache), &device);
  device->ResetBlockCounts();

  bcache = Bcache::Create(std::move(device), kBlockCount);
  ASSERT_TRUE(bcache.is_ok());
  MountOptions options = {};
  auto fs = Runner::Create(dispatcher(), *std::move(bcache), options);
  ASSERT_TRUE(fs.is_ok());

  // Perform the caller-requested operation.
  PerformOperation(fs->minfs());
  if (write_count_ == 0) {
    RecordWriteCount(fs->minfs());
  }

  TakeDeviceFromMinfs(*std::move(fs), &device);
  *in_out_device = std::move(device);
}

void JournalIntegrationFixture::PerformOperationWithTransactionLimit(
    uint64_t write_count, std::unique_ptr<FakeFVMBlockDevice>* in_out_device) {
  auto device = std::move(*in_out_device);
  auto bcache = Bcache::Create(std::move(device), kBlockCount);
  ASSERT_TRUE(bcache.is_ok());

  ASSERT_TRUE(Mkfs(bcache.value().get()).is_ok());
  // After formatting the device, create a transaction limit on the underlying device.
  TakeDeviceFromBcache(std::move(*bcache), &device);
  device->ResetBlockCounts();
  device->SetWriteBlockLimit(write_count);
  bcache = Bcache::Create(std::move(device), kBlockCount);
  ASSERT_TRUE(bcache.is_ok());
  MountOptions options = {};
  auto fs = Runner::Create(dispatcher(), *std::move(bcache), options);
  ASSERT_TRUE(fs.is_ok());

  // Perform the caller-requested operation.
  PerformOperation(fs->minfs());

  // Always do a sync (to match what happens in CountWritesToPerformOperation).
  sync_completion_t completion;
  fs->minfs().Sync([&completion](zx_status_t status) { sync_completion_signal(&completion); });
  ASSERT_EQ(sync_completion_wait(&completion, zx::duration::infinite().get()), ZX_OK);

  TakeDeviceFromMinfs(*std::move(fs), &device);
  device->ResetWriteBlockLimit();
  *in_out_device = std::move(device);
}

}  // namespace minfs
