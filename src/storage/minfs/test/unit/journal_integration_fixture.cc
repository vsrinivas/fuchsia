// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "journal_integration_fixture.h"

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
void TakeDeviceFromMinfs(std::unique_ptr<Minfs> minfs,
                         std::unique_ptr<block_client::FakeFVMBlockDevice>* out) {
  std::unique_ptr<Bcache> bcache = Minfs::Destroy(std::move(minfs));
  TakeDeviceFromBcache(std::move(bcache), out);
}

}  // namespace

void JournalIntegrationFixture::SetUp() {
  auto device =
      std::make_unique<FakeFVMBlockDevice>(kBlockCount, kBlockSize, kSliceSize, kSliceCount);
  ASSERT_NO_FAILURES(CountWritesToPerformOperation(&device));
}

std::unique_ptr<Bcache> JournalIntegrationFixture::CutOffDevice(uint64_t allowed_blocks) {
  auto device =
      std::make_unique<FakeFVMBlockDevice>(kBlockCount, kBlockSize, kSliceSize, kSliceCount);
  // Attempt to "cut-off" the operation partway by reducing the number of writes.
  PerformOperationWithTransactionLimit(allowed_blocks, &device);
  std::unique_ptr<Bcache> bcache;
  EXPECT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));
  return bcache;
}

void JournalIntegrationFixture::RecordWriteCount(Minfs* fs) {
  sync_completion_t completion;
  fs->Sync([&completion](zx_status_t status) { sync_completion_signal(&completion); });
  ASSERT_OK(sync_completion_wait(&completion, zx::duration::infinite().get()));
  write_count_ =
      static_cast<FakeFVMBlockDevice*>(fs->GetMutableBcache()->device())->GetWriteBlockCount();
}

void JournalIntegrationFixture::CountWritesToPerformOperation(
    std::unique_ptr<FakeFVMBlockDevice>* in_out_device) {
  auto device = std::move(*in_out_device);
  std::unique_ptr<Bcache> bcache;
  ASSERT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));

  ASSERT_OK(Mkfs(bcache.get()));
  // After formatting the device, count the number of blocks issued to the underlying device.
  TakeDeviceFromBcache(std::move(bcache), &device);
  device->ResetBlockCounts();
  ASSERT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));
  MountOptions options = {};
  std::unique_ptr<Minfs> fs;
  ASSERT_OK(Minfs::Create(std::move(bcache), options, &fs));

  // Perform the caller-requested operation.
  PerformOperation(fs.get());
  if (write_count_ == 0) {
    RecordWriteCount(fs.get());
  }

  TakeDeviceFromMinfs(std::move(fs), &device);
  *in_out_device = std::move(device);
}

void JournalIntegrationFixture::PerformOperationWithTransactionLimit(
    uint64_t write_count, std::unique_ptr<FakeFVMBlockDevice>* in_out_device) {
  auto device = std::move(*in_out_device);
  std::unique_ptr<Bcache> bcache;
  ASSERT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));

  ASSERT_OK(Mkfs(bcache.get()));
  // After formatting the device, create a transaction limit on the underlying device.
  TakeDeviceFromBcache(std::move(bcache), &device);
  device->ResetBlockCounts();
  device->SetWriteBlockLimit(write_count);
  ASSERT_OK(Bcache::Create(std::move(device), kBlockCount, &bcache));
  MountOptions options = {};
  std::unique_ptr<Minfs> fs;
  ASSERT_OK(Minfs::Create(std::move(bcache), options, &fs));

  // Perform the caller-requested operation.
  PerformOperation(fs.get());

  TakeDeviceFromMinfs(std::move(fs), &device);
  device->ResetWriteBlockLimit();
  *in_out_device = std::move(device);
}

}  // namespace minfs
