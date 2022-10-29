// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/block_client/cpp/fake_block_device.h"

#include <zircon/errors.h>

#include <array>
#include <iterator>

#include <gtest/gtest.h>
#include <storage/buffer/owned_vmoid.h>

#include "src/storage/fvm/format.h"

namespace block_client {
namespace {

constexpr uint64_t kBlockCountDefault = 1024;
constexpr uint32_t kBlockSizeDefault = 512;
constexpr uint64_t kSliceSizeDefault = 1024;
constexpr uint64_t kSliceCountDefault = 128;

TEST(FakeBlockDeviceTest, EmptyDevice) {
  const uint64_t kBlockCount = 0;
  const uint32_t kBlockSize = 0;
  std::unique_ptr<BlockDevice> device = std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
  fuchsia_hardware_block::wire::BlockInfo info = {};
  ASSERT_EQ(device->BlockGetInfo(&info), ZX_OK);
  EXPECT_EQ(kBlockCount, info.block_count);
  EXPECT_EQ(kBlockSize, info.block_size);
  EXPECT_EQ(info.flags, 0u);
  EXPECT_EQ(fuchsia_hardware_block::wire::kMaxTransferUnbounded, info.max_transfer_size);
}

TEST(FakeBlockDeviceTest, NonEmptyDevice) {
  std::unique_ptr<BlockDevice> device = std::make_unique<FakeBlockDevice>(
      FakeBlockDevice::Config{.block_count = kBlockCountDefault,
                              .block_size = kBlockSizeDefault,
                              .supports_trim = true,
                              .max_transfer_size = kBlockCountDefault * 8});
  fuchsia_hardware_block::wire::BlockInfo info = {};
  ASSERT_EQ(device->BlockGetInfo(&info), ZX_OK);
  EXPECT_EQ(kBlockCountDefault, info.block_count);
  EXPECT_EQ(kBlockSizeDefault, info.block_size);
  EXPECT_TRUE(info.flags & fuchsia_hardware_block::wire::kFlagTrimSupport);
  EXPECT_EQ(kBlockCountDefault * 8, info.max_transfer_size);
}

void CreateAndRegisterVmo(BlockDevice* device, size_t blocks, zx::vmo* vmo,
                          storage::OwnedVmoid* vmoid) {
  fuchsia_hardware_block::wire::BlockInfo info = {};
  ASSERT_EQ(device->BlockGetInfo(&info), ZX_OK);
  ASSERT_EQ(zx::vmo::create(blocks * info.block_size, 0, vmo), ZX_OK);
  ASSERT_EQ(device->BlockAttachVmo(*vmo, &vmoid->GetReference(device)), ZX_OK);
}

TEST(FakeBlockDeviceTest, WriteAndReadUsingFifoTransaction) {
  auto fake_device = std::make_unique<FakeBlockDevice>(kBlockCountDefault, kBlockSizeDefault);
  BlockDevice* device = fake_device.get();

  const size_t kVmoBlocks = 4;
  zx::vmo vmo;
  storage::OwnedVmoid vmoid;
  ASSERT_NO_FATAL_FAILURE(CreateAndRegisterVmo(device, kVmoBlocks, &vmo, &vmoid));

  // Write some data to the device.
  char src[kVmoBlocks * kBlockSizeDefault];
  memset(src, 'a', sizeof(src));
  ASSERT_EQ(vmo.write(src, 0, sizeof(src)), ZX_OK);
  block_fifo_request_t request;
  request.opcode = BLOCKIO_WRITE;
  request.vmoid = vmoid.get();
  request.length = kVmoBlocks;
  request.vmo_offset = 0;
  request.dev_offset = 0;
  ASSERT_EQ(device->FifoTransaction(&request, 1), ZX_OK);

  fuchsia_hardware_block::wire::BlockStats stats;
  fake_device->GetStats(false, &stats);
  ASSERT_EQ(stats.write.success.total_calls, 1u);
  ASSERT_EQ(kVmoBlocks * kBlockSizeDefault, stats.write.success.bytes_transferred);
  ASSERT_GE(stats.write.success.total_time_spent, 0u);

  // Clear out the registered VMO.
  char dst[kVmoBlocks * kBlockSizeDefault];
  static_assert(sizeof(src) == sizeof(dst), "Mismatched input/output buffer size");
  memset(dst, 0, sizeof(dst));
  ASSERT_EQ(vmo.write(dst, 0, sizeof(dst)), ZX_OK);

  // Read data from the fake back into the registered VMO.
  request.opcode = BLOCKIO_READ;
  request.vmoid = vmoid.get();
  request.length = kVmoBlocks;
  request.vmo_offset = 0;
  request.dev_offset = 0;
  ASSERT_EQ(device->FifoTransaction(&request, 1), ZX_OK);
  ASSERT_EQ(vmo.read(dst, 0, sizeof(dst)), ZX_OK);
  EXPECT_EQ(memcmp(src, dst, sizeof(src)), 0);

  fake_device->GetStats(false, &stats);
  ASSERT_EQ(stats.read.success.total_calls, 1u);
  ASSERT_EQ(kVmoBlocks * kBlockSizeDefault, stats.read.success.bytes_transferred);
  ASSERT_GE(stats.read.success.total_time_spent, 0u);
}

TEST(FakeBlockDeviceTest, FifoTransactionFlush) {
  auto fake_device = std::make_unique<FakeBlockDevice>(kBlockCountDefault, kBlockSizeDefault);
  BlockDevice* device = fake_device.get();

  const size_t kVmoBlocks = 1;
  zx::vmo vmo;
  storage::OwnedVmoid vmoid;
  ASSERT_NO_FATAL_FAILURE(CreateAndRegisterVmo(device, kVmoBlocks, &vmo, &vmoid));

  block_fifo_request_t request;
  request.opcode = BLOCKIO_FLUSH;
  request.vmoid = vmoid.get();
  request.length = 0;
  request.vmo_offset = 0;
  request.dev_offset = 0;
  EXPECT_EQ(device->FifoTransaction(&request, 1), ZX_OK);

  fuchsia_hardware_block::wire::BlockStats stats;
  fake_device->GetStats(false, &stats);
  ASSERT_EQ(stats.flush.success.total_calls, 1u);
  ASSERT_EQ(stats.flush.success.bytes_transferred, 0u);
  ASSERT_GE(stats.flush.success.total_time_spent, 0u);
}

// Tests that writing followed by a flush acts like a regular write.
TEST(FakeBlockDeviceTest, FifoTransactionWriteThenFlush) {
  std::unique_ptr<BlockDevice> device =
      std::make_unique<FakeBlockDevice>(kBlockCountDefault, kBlockSizeDefault);

  const size_t kVmoBlocks = 1;
  zx::vmo vmo;
  storage::OwnedVmoid vmoid;
  ASSERT_NO_FATAL_FAILURE(CreateAndRegisterVmo(device.get(), kVmoBlocks, &vmo, &vmoid));

  char src[kVmoBlocks * kBlockSizeDefault];
  memset(src, 'a', sizeof(src));
  ASSERT_EQ(vmo.write(src, 0, sizeof(src)), ZX_OK);

  block_fifo_request_t requests[2];
  requests[0].opcode = BLOCKIO_WRITE;
  requests[0].vmoid = vmoid.get();
  requests[0].length = kVmoBlocks;
  requests[0].vmo_offset = 0;
  requests[0].dev_offset = 0;

  requests[1].opcode = BLOCKIO_FLUSH;
  requests[1].vmoid = vmoid.get();
  requests[1].length = 0;
  requests[1].vmo_offset = 0;
  requests[1].dev_offset = 0;
  EXPECT_EQ(device->FifoTransaction(requests, std::size(requests)), ZX_OK);

  char dst[kVmoBlocks * kBlockSizeDefault];
  static_assert(sizeof(src) == sizeof(dst), "Mismatched input/output buffer size");
  memset(dst, 0, sizeof(dst));
  ASSERT_EQ(vmo.write(dst, 0, sizeof(dst)), ZX_OK);

  block_fifo_request_t request;
  request.opcode = BLOCKIO_READ;
  request.vmoid = vmoid.get();
  request.length = kVmoBlocks;
  request.vmo_offset = 0;
  request.dev_offset = 0;
  ASSERT_EQ(device->FifoTransaction(&request, 1), ZX_OK);
  ASSERT_EQ(vmo.read(dst, 0, sizeof(dst)), ZX_OK);
  EXPECT_EQ(memcmp(src, dst, sizeof(src)), 0);
}

// Tests that flushing followed by a write acts like a regular write.
TEST(FakeBlockDeviceTest, FifoTransactionFlushThenWrite) {
  std::unique_ptr<BlockDevice> device =
      std::make_unique<FakeBlockDevice>(kBlockCountDefault, kBlockSizeDefault);

  const size_t kVmoBlocks = 1;
  zx::vmo vmo;
  storage::OwnedVmoid vmoid;
  ASSERT_NO_FATAL_FAILURE(CreateAndRegisterVmo(device.get(), kVmoBlocks, &vmo, &vmoid));

  char src[kVmoBlocks * kBlockSizeDefault];
  memset(src, 'a', sizeof(src));
  ASSERT_EQ(vmo.write(src, 0, sizeof(src)), ZX_OK);

  block_fifo_request_t requests[2];
  requests[0].opcode = BLOCKIO_FLUSH;
  requests[0].vmoid = vmoid.get();
  requests[0].length = 0;
  requests[0].vmo_offset = 0;
  requests[0].dev_offset = 0;

  requests[1].opcode = BLOCKIO_WRITE;
  requests[1].vmoid = vmoid.get();
  requests[1].length = kVmoBlocks;
  requests[1].vmo_offset = 0;
  requests[1].dev_offset = 0;

  EXPECT_EQ(device->FifoTransaction(requests, std::size(requests)), ZX_OK);

  char dst[kVmoBlocks * kBlockSizeDefault];
  static_assert(sizeof(src) == sizeof(dst), "Mismatched input/output buffer size");
  memset(dst, 0, sizeof(dst));
  ASSERT_EQ(vmo.write(dst, 0, sizeof(dst)), ZX_OK);

  block_fifo_request_t request;
  request.opcode = BLOCKIO_READ;
  request.vmoid = vmoid.get();
  request.length = kVmoBlocks;
  request.vmo_offset = 0;
  request.dev_offset = 0;
  ASSERT_EQ(device->FifoTransaction(&request, 1), ZX_OK);
  ASSERT_EQ(vmo.read(dst, 0, sizeof(dst)), ZX_OK);
  EXPECT_EQ(memcmp(src, dst, sizeof(src)), 0);
}

TEST(FakeBlockDeviceTest, FifoTransactionClose) {
  auto fake_device = std::make_unique<FakeBlockDevice>(kBlockCountDefault, kBlockSizeDefault);
  auto device = reinterpret_cast<BlockDevice*>(fake_device.get());

  const size_t kVmoBlocks = 1;
  zx::vmo vmo;
  storage::OwnedVmoid vmoid;
  ASSERT_NO_FATAL_FAILURE(CreateAndRegisterVmo(device, kVmoBlocks, &vmo, &vmoid));
  vmoid_t id = vmoid.TakeId();

  block_fifo_request_t request;
  request.opcode = BLOCKIO_CLOSE_VMO;
  request.vmoid = id;
  request.length = 0;
  request.vmo_offset = 0;
  request.dev_offset = 0;

  EXPECT_TRUE(fake_device->IsRegistered(id));
  EXPECT_EQ(device->FifoTransaction(&request, 1), ZX_OK);
  EXPECT_FALSE(fake_device->IsRegistered(id));
}

TEST(FakeBlockDeviceTest, FifoTransactionUnsupportedTrim) {
  auto fake_device = std::make_unique<FakeBlockDevice>(kBlockCountDefault, kBlockSizeDefault);
  BlockDevice* device = fake_device.get();

  const size_t kVmoBlocks = 1;
  zx::vmo vmo;
  storage::OwnedVmoid vmoid;
  ASSERT_NO_FATAL_FAILURE(CreateAndRegisterVmo(device, kVmoBlocks, &vmo, &vmoid));

  block_fifo_request_t request;
  request.opcode = BLOCKIO_TRIM;
  request.vmoid = vmoid.get();
  request.length = kVmoBlocks;
  request.vmo_offset = 0;
  request.dev_offset = 0;
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, device->FifoTransaction(&request, 1));

  fuchsia_hardware_block::wire::BlockStats stats;
  fake_device->GetStats(true, &stats);
  ASSERT_EQ(stats.trim.failure.total_calls, 1u);
  ASSERT_EQ(kVmoBlocks * kBlockSizeDefault, stats.trim.failure.bytes_transferred);
  ASSERT_GE(stats.trim.failure.total_time_spent, 0u);
}

TEST(FakeBlockDeviceTest, ClearStats) {
  auto fake_device = std::make_unique<FakeBlockDevice>(kBlockCountDefault, kBlockSizeDefault);
  BlockDevice* device = fake_device.get();

  const size_t kVmoBlocks = 1;
  zx::vmo vmo;
  storage::OwnedVmoid vmoid;
  ASSERT_NO_FATAL_FAILURE(CreateAndRegisterVmo(device, kVmoBlocks, &vmo, &vmoid));

  block_fifo_request_t request;
  request.opcode = BLOCKIO_FLUSH;
  request.vmoid = vmoid.get();
  request.length = 0;
  request.vmo_offset = 0;
  request.dev_offset = 0;
  EXPECT_EQ(device->FifoTransaction(&request, 1), ZX_OK);

  fuchsia_hardware_block::wire::BlockStats stats;
  fake_device->GetStats(true, &stats);
  ASSERT_EQ(stats.flush.success.total_calls, 1u);
  ASSERT_EQ(stats.flush.success.bytes_transferred, 0u);
  ASSERT_GE(stats.flush.success.total_time_spent, 0u);

  // We cleared stats during previous GetStats.
  fake_device->GetStats(false, &stats);
  ASSERT_EQ(stats.flush.success.total_calls, 0u);
  ASSERT_EQ(stats.flush.success.bytes_transferred, 0u);
  ASSERT_EQ(stats.flush.success.total_time_spent, 0u);
}

TEST(FakeBlockDeviceTest, BlockLimitPartialyFailTransaction) {
  auto device = std::make_unique<FakeBlockDevice>(kBlockCountDefault, kBlockSizeDefault);

  const size_t kVmoBlocks = 4;
  const size_t kLimitBlocks = 2;
  zx::vmo vmo;
  storage::OwnedVmoid vmoid;
  ASSERT_NO_FATAL_FAILURE(CreateAndRegisterVmo(device.get(), kVmoBlocks, &vmo, &vmoid));

  // Pre-fill the source buffer.
  std::array<uint8_t, kBlockSizeDefault> block;
  memset(block.data(), 'a', block.size());
  for (size_t i = 0; i < kVmoBlocks; i++) {
    ASSERT_EQ(vmo.write(block.data(), i * block.size(), block.size()), ZX_OK);
  }

  block_fifo_request_t request;
  request.opcode = BLOCKIO_WRITE;
  request.vmoid = vmoid.get();
  request.length = kVmoBlocks;
  request.vmo_offset = 0;
  request.dev_offset = 0;

  // First, set the transaction limit.
  EXPECT_EQ(device->GetWriteBlockCount(), 0u);
  device->SetWriteBlockLimit(2);

  ASSERT_EQ(ZX_ERR_IO, device->FifoTransaction(&request, 1));
  EXPECT_EQ(device->GetWriteBlockCount(), 2u);

  // Read from the device, an observe that the operation was only partially
  // successful.
  std::array<uint8_t, kBlockSizeDefault> zero_block;
  memset(zero_block.data(), 0, zero_block.size());
  for (size_t i = 0; i < kVmoBlocks; i++) {
    ASSERT_EQ(vmo.write(zero_block.data(), i * zero_block.size(), zero_block.size()), ZX_OK);
  }

  request.opcode = BLOCKIO_READ;
  ASSERT_EQ(device->FifoTransaction(&request, 1), ZX_OK);

  // Expect to see valid data for the two blocks that were written.
  for (size_t i = 0; i < kLimitBlocks; i++) {
    std::array<uint8_t, kBlockSizeDefault> dst;
    ASSERT_EQ(vmo.read(dst.data(), i * dst.size(), dst.size()), ZX_OK);
    ASSERT_EQ(memcmp(block.data(), dst.data(), dst.size()), 0);
  }
  // Expect to see zero for the two blocks that were not written.
  for (size_t i = kLimitBlocks; i < kVmoBlocks; i++) {
    std::array<uint8_t, kBlockSizeDefault> dst;
    ASSERT_EQ(vmo.read(dst.data(), i * dst.size(), dst.size()), ZX_OK);
    ASSERT_EQ(memcmp(zero_block.data(), dst.data(), dst.size()), 0);
  }
}
TEST(FakeBlockDeviceTest, BlockLimitFailsDistinctTransactions) {
  auto device = std::make_unique<FakeBlockDevice>(kBlockCountDefault, kBlockSizeDefault);

  const size_t kVmoBlocks = 1;
  zx::vmo vmo;
  storage::OwnedVmoid vmoid;
  ASSERT_NO_FATAL_FAILURE(CreateAndRegisterVmo(device.get(), kVmoBlocks, &vmo, &vmoid));

  block_fifo_request_t request;
  request.opcode = BLOCKIO_WRITE;
  request.vmoid = vmoid.get();
  request.length = kVmoBlocks;
  request.vmo_offset = 0;
  request.dev_offset = 0;

  // First, set the transaction limit.
  EXPECT_EQ(device->GetWriteBlockCount(), 0u);
  device->SetWriteBlockLimit(3);

  // Observe that we can fulfill three transactions...
  EXPECT_EQ(ZX_OK, device->FifoTransaction(&request, 1));
  EXPECT_EQ(ZX_OK, device->FifoTransaction(&request, 1));
  EXPECT_EQ(ZX_OK, device->FifoTransaction(&request, 1));

  // ... But then we see an I/O failure.
  EXPECT_EQ(device->GetWriteBlockCount(), 3u);
  EXPECT_EQ(ZX_ERR_IO, device->FifoTransaction(&request, 1));
}

TEST(FakeBlockDeviceTest, BlockLimitFailsMergedTransactions) {
  auto device = std::make_unique<FakeBlockDevice>(kBlockCountDefault, kBlockSizeDefault);

  const size_t kVmoBlocks = 1;
  zx::vmo vmo;
  storage::OwnedVmoid vmoid;
  ASSERT_NO_FATAL_FAILURE(CreateAndRegisterVmo(device.get(), kVmoBlocks, &vmo, &vmoid));

  constexpr size_t kRequests = 3;
  block_fifo_request_t requests[kRequests];
  for (auto& request : requests) {
    request.opcode = BLOCKIO_WRITE;
    request.vmoid = vmoid.get();
    request.length = kVmoBlocks;
    request.vmo_offset = 0;
    request.dev_offset = 0;
  }

  // First, set the transaction limit.
  device->SetWriteBlockLimit(3);

  // Observe that we can fulfill three transactions...
  EXPECT_EQ(ZX_OK, device->FifoTransaction(requests, kRequests));

  // ... But then we see an I/O failure.
  EXPECT_EQ(ZX_ERR_IO, device->FifoTransaction(requests, 1));
}

TEST(FakeBlockDeviceTest, BlockLimitResetsDevice) {
  auto device = std::make_unique<FakeBlockDevice>(kBlockCountDefault, kBlockSizeDefault);

  const size_t kVmoBlocks = 1;
  zx::vmo vmo;
  storage::OwnedVmoid vmoid;
  ASSERT_NO_FATAL_FAILURE(CreateAndRegisterVmo(device.get(), kVmoBlocks, &vmo, &vmoid));

  block_fifo_request_t request;
  request.opcode = BLOCKIO_WRITE;
  request.vmoid = vmoid.get();
  request.length = kVmoBlocks;
  request.vmo_offset = 0;
  request.dev_offset = 0;

  // First, set the transaction limit.
  device->SetWriteBlockLimit(2);

  // Observe that we can fail the device...
  EXPECT_EQ(ZX_OK, device->FifoTransaction(&request, 1));
  EXPECT_EQ(ZX_OK, device->FifoTransaction(&request, 1));
  EXPECT_EQ(ZX_ERR_IO, device->FifoTransaction(&request, 1));

  // ... But we can reset the device by supplying a different transaction limit.
  device->ResetWriteBlockLimit();
  EXPECT_EQ(ZX_OK, device->FifoTransaction(&request, 1));
}

TEST(FakeBlockDeviceTest, Hook) {
  FakeBlockDevice device(kBlockCountDefault, kBlockSizeDefault);

  const size_t kVmoBlocks = 1;
  zx::vmo vmo;
  storage::OwnedVmoid vmoid;
  ASSERT_NO_FATAL_FAILURE(CreateAndRegisterVmo(&device, kVmoBlocks, &vmo, &vmoid));
  char v = 1;
  ASSERT_EQ(vmo.write(&v, 0, 1), ZX_OK);

  block_fifo_request_t request = {
      .opcode = BLOCKIO_WRITE,
      .vmoid = vmoid.get(),
      .length = 5555,
      .vmo_offset = 1234,
      .dev_offset = 5678,
  };
  device.set_hook([&](const block_fifo_request_t& request, const zx::vmo* vmo) {
    EXPECT_NE(vmo, nullptr);
    if (vmo) {
      char v = 0;
      EXPECT_EQ(vmo->read(&v, 0, 1), ZX_OK);
      EXPECT_EQ(v, 1);
    }
    EXPECT_EQ(request.opcode, uint32_t{BLOCKIO_WRITE});
    EXPECT_EQ(request.vmo_offset, 1234u);
    EXPECT_EQ(request.dev_offset, 5678u);
    EXPECT_EQ(request.length, 5555u);
    EXPECT_EQ(request.vmoid, vmoid.get());
    return ZX_ERR_WRONG_TYPE;
  });
  EXPECT_EQ(device.FifoTransaction(&request, 1), ZX_ERR_WRONG_TYPE);
  device.set_hook({});
}

TEST(FakeBlockDeviceTest, WipeZeroesDevice) {
  FakeBlockDevice device(kBlockCountDefault, kBlockSizeDefault);

  const size_t kVmoBlocks = 1;
  zx::vmo vmo;
  storage::OwnedVmoid vmoid;
  ASSERT_NO_FATAL_FAILURE(CreateAndRegisterVmo(&device, kVmoBlocks, &vmo, &vmoid));
  char v = 1;
  ASSERT_EQ(vmo.write(&v, 0, 1), ZX_OK);

  block_fifo_request_t request = {
      .opcode = BLOCKIO_WRITE,
      .vmoid = vmoid.get(),
      .length = 1,
      .vmo_offset = 0,
      .dev_offset = 700,
  };
  EXPECT_EQ(device.FifoTransaction(&request, 1), ZX_OK);

  device.Wipe();

  request.opcode = BLOCKIO_READ;
  request.vmo_offset = 1;
  EXPECT_EQ(device.FifoTransaction(&request, 1), ZX_OK);

  EXPECT_EQ(vmo.read(&v, kBlockSizeDefault, 1), ZX_OK);
  EXPECT_EQ(v, 0);
}

TEST(FakeBlockDeviceTest, TrimFailsIfUnsupported) {
  FakeBlockDevice device(
      {.block_count = kBlockCountDefault, .block_size = kBlockSizeDefault, .supports_trim = false});

  block_fifo_request_t request = {
      .opcode = BLOCKIO_TRIM,
      .vmoid = BLOCK_VMOID_INVALID,
      .length = 1,
      .vmo_offset = 0,
      .dev_offset = 700,
  };
  EXPECT_EQ(device.FifoTransaction(&request, 1), ZX_ERR_NOT_SUPPORTED);
}

TEST(FakeBlockDeviceTest, TrimSucceedsIfSupported) {
  FakeBlockDevice device(
      {.block_count = kBlockCountDefault, .block_size = kBlockSizeDefault, .supports_trim = true});

  block_fifo_request_t request = {
      .opcode = BLOCKIO_TRIM,
      .vmoid = BLOCK_VMOID_INVALID,
      .length = 1,
      .vmo_offset = 0,
      .dev_offset = 700,
  };
  EXPECT_EQ(device.FifoTransaction(&request, 1), ZX_OK);
}

TEST(FakeFVMBlockDeviceTest, GetVolumeInfo) {
  std::unique_ptr<BlockDevice> device = std::make_unique<FakeFVMBlockDevice>(
      kBlockCountDefault, kBlockSizeDefault, kSliceSizeDefault, kSliceCountDefault);
  {
    fuchsia_hardware_block::wire::BlockInfo info = {};
    ASSERT_EQ(device->BlockGetInfo(&info), ZX_OK);
    EXPECT_EQ(kBlockCountDefault, info.block_count);
    EXPECT_EQ(kBlockSizeDefault, info.block_size);
  }

  {
    fuchsia_hardware_block_volume::wire::VolumeManagerInfo manager_info = {};
    fuchsia_hardware_block_volume::wire::VolumeInfo volume_info = {};
    ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
    EXPECT_EQ(kSliceSizeDefault, manager_info.slice_size);
    EXPECT_EQ(manager_info.assigned_slice_count, 1u);
  }
}

TEST(FakeFVMBlockDeviceTest, QuerySlices) {
  std::unique_ptr<BlockDevice> device = std::make_unique<FakeFVMBlockDevice>(
      kBlockCountDefault, kBlockSizeDefault, kSliceSizeDefault, kSliceCountDefault);
  uint64_t slice_start = 0;
  size_t slice_count = 1;
  fuchsia_hardware_block_volume::wire::VsliceRange ranges;
  size_t actual_ranges = 0;
  ASSERT_EQ(ZX_OK, device->VolumeQuerySlices(&slice_start, slice_count, &ranges, &actual_ranges));
  ASSERT_EQ(actual_ranges, 1u);
  EXPECT_TRUE(ranges.allocated);
  EXPECT_EQ(ranges.count, 1u);

  slice_start = 1;
  actual_ranges = 0;
  ASSERT_EQ(ZX_OK, device->VolumeQuerySlices(&slice_start, slice_count, &ranges, &actual_ranges));
  ASSERT_EQ(actual_ranges, 1u);
  EXPECT_FALSE(ranges.allocated);
  EXPECT_EQ(fvm::kMaxVSlices - 1, ranges.count);

  slice_start = fvm::kMaxVSlices;
  actual_ranges = 0;
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE,
            device->VolumeQuerySlices(&slice_start, slice_count, &ranges, &actual_ranges));
  ASSERT_EQ(actual_ranges, 0u);
}

void CheckAllocatedSlices(BlockDevice* device, const uint64_t* starts, const uint64_t* lengths,
                          size_t slices_count) {
  fuchsia_hardware_block_volume::wire::VsliceRange ranges[slices_count];
  size_t actual_ranges = 0;
  ASSERT_EQ(ZX_OK, device->VolumeQuerySlices(starts, slices_count, ranges, &actual_ranges));
  ASSERT_EQ(slices_count, actual_ranges);
  for (size_t i = 0; i < slices_count; i++) {
    EXPECT_TRUE(ranges[i].allocated);
    EXPECT_EQ(lengths[i], ranges[i].count);
  }
}

TEST(FakeFVMBlockDeviceTest, ExtendNoOp) {
  std::unique_ptr<BlockDevice> device = std::make_unique<FakeFVMBlockDevice>(
      kBlockCountDefault, kBlockSizeDefault, kSliceSizeDefault, kSliceCountDefault);

  fuchsia_hardware_block_volume::wire::VolumeManagerInfo manager_info = {};
  fuchsia_hardware_block_volume::wire::VolumeInfo volume_info = {};
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 1u);

  ASSERT_EQ(device->VolumeExtend(0, 0), ZX_OK);
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 1u);

  uint64_t starts = 0;
  uint64_t lengths = 1;
  ASSERT_NO_FATAL_FAILURE(CheckAllocatedSlices(device.get(), &starts, &lengths, 1));
}

TEST(FakeFVMBlockDeviceTest, ExtendOverlappingSameStart) {
  std::unique_ptr<BlockDevice> device = std::make_unique<FakeFVMBlockDevice>(
      kBlockCountDefault, kBlockSizeDefault, kSliceSizeDefault, kSliceCountDefault);

  fuchsia_hardware_block_volume::wire::VolumeManagerInfo manager_info = {};
  fuchsia_hardware_block_volume::wire::VolumeInfo volume_info = {};
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 1u);

  ASSERT_EQ(device->VolumeExtend(0, 2), ZX_OK);
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 2u);

  uint64_t starts = 0;
  uint64_t lengths = 2;
  ASSERT_NO_FATAL_FAILURE(CheckAllocatedSlices(device.get(), &starts, &lengths, 1));
}

TEST(FakeFVMBlockDeviceTest, ExtendOverlappingDifferentStart) {
  std::unique_ptr<BlockDevice> device = std::make_unique<FakeFVMBlockDevice>(
      kBlockCountDefault, kBlockSizeDefault, kSliceSizeDefault, kSliceCountDefault);

  fuchsia_hardware_block_volume::wire::VolumeManagerInfo manager_info = {};
  fuchsia_hardware_block_volume::wire::VolumeInfo volume_info = {};
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 1u);

  ASSERT_EQ(device->VolumeExtend(1, 2), ZX_OK);
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 3u);

  uint64_t starts = 0;
  uint64_t lengths = 3;
  ASSERT_NO_FATAL_FAILURE(CheckAllocatedSlices(device.get(), &starts, &lengths, 1));
}

TEST(FakeFVMBlockDeviceTest, ExtendNonOverlapping) {
  std::unique_ptr<BlockDevice> device = std::make_unique<FakeFVMBlockDevice>(
      kBlockCountDefault, kBlockSizeDefault, kSliceSizeDefault, kSliceCountDefault);

  fuchsia_hardware_block_volume::wire::VolumeManagerInfo manager_info = {};
  fuchsia_hardware_block_volume::wire::VolumeInfo volume_info = {};
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 1u);

  ASSERT_EQ(device->VolumeExtend(2, 2), ZX_OK);
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 3u);

  uint64_t starts[2] = {0, 2};
  uint64_t lengths[2] = {1, 2};
  ASSERT_NO_FATAL_FAILURE(CheckAllocatedSlices(device.get(), starts, lengths, 2));
}

TEST(FakeFVMBlockDeviceTest, ShrinkNoOp) {
  std::unique_ptr<BlockDevice> device = std::make_unique<FakeFVMBlockDevice>(
      kBlockCountDefault, kBlockSizeDefault, kSliceSizeDefault, kSliceCountDefault);

  fuchsia_hardware_block_volume::wire::VolumeManagerInfo manager_info = {};
  fuchsia_hardware_block_volume::wire::VolumeInfo volume_info = {};
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 1u);

  ASSERT_EQ(device->VolumeShrink(0, 0), ZX_OK);
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 1u);
}

TEST(FakeFVMBlockDeviceTest, ShrinkInvalid) {
  std::unique_ptr<BlockDevice> device = std::make_unique<FakeFVMBlockDevice>(
      kBlockCountDefault, kBlockSizeDefault, kSliceSizeDefault, kSliceCountDefault);

  fuchsia_hardware_block_volume::wire::VolumeManagerInfo manager_info = {};
  fuchsia_hardware_block_volume::wire::VolumeInfo volume_info = {};
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 1u);

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, device->VolumeShrink(100, 5));
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 1u);
}

// [0, 0) -> Extend
// [0, 11) -> Shrink
// [0, 0)
TEST(FakeFVMBlockDeviceTest, ExtendThenShrinkSubSection) {
  std::unique_ptr<BlockDevice> device = std::make_unique<FakeFVMBlockDevice>(
      kBlockCountDefault, kBlockSizeDefault, kSliceSizeDefault, kSliceCountDefault);

  fuchsia_hardware_block_volume::wire::VolumeManagerInfo manager_info = {};
  fuchsia_hardware_block_volume::wire::VolumeInfo volume_info = {};
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 1u);

  ASSERT_EQ(device->VolumeExtend(1, 10), ZX_OK);
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 11u);

  ASSERT_EQ(device->VolumeShrink(1, 10), ZX_OK);
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 1u);

  uint64_t starts[1] = {0};
  uint64_t lengths[1] = {1};
  ASSERT_NO_FATAL_FAILURE(CheckAllocatedSlices(device.get(), starts, lengths, 1));
}

// [0, 0) -> Extend
// [0, 0) + [5, 15) -> Shrink
// [0, 0) + [6, 15) -> Shrink
// [0, 0) + [6, 14)
TEST(FakeFVMBlockDeviceTest, ExtendThenShrinkPartialOverlap) {
  std::unique_ptr<BlockDevice> device = std::make_unique<FakeFVMBlockDevice>(
      kBlockCountDefault, kBlockSizeDefault, kSliceSizeDefault, kSliceCountDefault);

  fuchsia_hardware_block_volume::wire::VolumeManagerInfo manager_info = {};
  fuchsia_hardware_block_volume::wire::VolumeInfo volume_info = {};
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 1u);

  ASSERT_EQ(device->VolumeExtend(5, 10), ZX_OK);
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 11u);

  // One slice overlaps, one doesn't.
  ASSERT_EQ(device->VolumeShrink(4, 2), ZX_OK);
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 10u);

  // One slice overlaps, one doesn't.
  ASSERT_EQ(device->VolumeShrink(14, 2), ZX_OK);
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 9u);

  uint64_t starts[2] = {0, 6};
  uint64_t lengths[2] = {1, 8};
  ASSERT_NO_FATAL_FAILURE(CheckAllocatedSlices(device.get(), starts, lengths, 2));
}

// [0, 0) -> Extend
// [0, 0) + [5, 15) -> Shrink
// [0, 0)
TEST(FakeFVMBlockDeviceTest, ExtendThenShrinkTotal) {
  std::unique_ptr<BlockDevice> device = std::make_unique<FakeFVMBlockDevice>(
      kBlockCountDefault, kBlockSizeDefault, kSliceSizeDefault, kSliceCountDefault);

  fuchsia_hardware_block_volume::wire::VolumeManagerInfo manager_info = {};
  fuchsia_hardware_block_volume::wire::VolumeInfo volume_info = {};
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 1u);

  ASSERT_EQ(device->VolumeExtend(5, 10), ZX_OK);
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 11u);

  ASSERT_EQ(device->VolumeShrink(5, 10), ZX_OK);
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 1u);

  uint64_t starts[1] = {0};
  uint64_t lengths[1] = {1};
  ASSERT_NO_FATAL_FAILURE(CheckAllocatedSlices(device.get(), starts, lengths, 1));
}

// [0, 0) -> Extend
// [0, 0) + [5, 15) -> Shrink
// [0, 0) + [5, 6) + [9, 15)
TEST(FakeFVMBlockDeviceTest, ExtendThenShrinkToSplit) {
  std::unique_ptr<BlockDevice> device = std::make_unique<FakeFVMBlockDevice>(
      kBlockCountDefault, kBlockSizeDefault, kSliceSizeDefault, kSliceCountDefault);

  fuchsia_hardware_block_volume::wire::VolumeManagerInfo manager_info = {};
  fuchsia_hardware_block_volume::wire::VolumeInfo volume_info = {};
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 1u);

  ASSERT_EQ(device->VolumeExtend(5, 10), ZX_OK);
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 11u);

  ASSERT_EQ(device->VolumeShrink(7, 2), ZX_OK);
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 9u);

  uint64_t starts[3] = {0, 5, 9};
  uint64_t lengths[3] = {1, 2, 6};
  ASSERT_NO_FATAL_FAILURE(CheckAllocatedSlices(device.get(), starts, lengths, 3));
}

// [0, 0) -> Extend
// [0, 10) -> Extend (overallocate)
// [0, 10) -> Shrink
// [0, 9) -> Extend
// [0, 9)
TEST(FakeFVMBlockDeviceTest, OverallocateSlices) {
  const uint64_t kSliceCapacity = 10;
  std::unique_ptr<BlockDevice> device = std::make_unique<FakeFVMBlockDevice>(
      kBlockCountDefault, kBlockSizeDefault, kSliceSizeDefault, kSliceCapacity);

  fuchsia_hardware_block_volume::wire::VolumeManagerInfo manager_info = {};
  fuchsia_hardware_block_volume::wire::VolumeInfo volume_info = {};
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 1u);
  EXPECT_EQ(kSliceCapacity, manager_info.slice_count);

  // Allocate all slices.
  ASSERT_EQ(device->VolumeExtend(1, manager_info.slice_count - manager_info.assigned_slice_count),
            ZX_OK);
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(kSliceCapacity, manager_info.assigned_slice_count);

  // We cannot allocate more slices without remaining space.
  ASSERT_EQ(ZX_ERR_NO_SPACE, device->VolumeExtend(kSliceCapacity, 1));

  // However, if we shrink an earlier slice, we can re-allocate.
  ASSERT_EQ(device->VolumeShrink(kSliceCapacity - 1, 1), ZX_OK);
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(kSliceCapacity - 1, manager_info.assigned_slice_count);
  ASSERT_EQ(device->VolumeExtend(kSliceCapacity, 1), ZX_OK);
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(kSliceCapacity, manager_info.assigned_slice_count);

  uint64_t starts[2] = {0, kSliceCapacity};
  uint64_t lengths[2] = {kSliceCapacity - 1, 1};
  ASSERT_NO_FATAL_FAILURE(CheckAllocatedSlices(device.get(), starts, lengths, 2));
}

// [0, 0) -> Extend (overallocate)
// [0, 0)
TEST(FakeFVMBlockDeviceTest, PartialOverallocateSlices) {
  const uint64_t kSliceCapacity = 10;
  std::unique_ptr<BlockDevice> device = std::make_unique<FakeFVMBlockDevice>(
      kBlockCountDefault, kBlockSizeDefault, kSliceSizeDefault, kSliceCapacity);

  fuchsia_hardware_block_volume::wire::VolumeManagerInfo manager_info = {};
  fuchsia_hardware_block_volume::wire::VolumeInfo volume_info = {};
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 1u);
  EXPECT_EQ(kSliceCapacity, manager_info.slice_count);

  // Allocating too many slices up front should not allocate any slices.
  ASSERT_EQ(ZX_ERR_NO_SPACE, device->VolumeExtend(1, manager_info.slice_count));
  ASSERT_EQ(device->VolumeGetInfo(&manager_info, &volume_info), ZX_OK);
  EXPECT_EQ(manager_info.assigned_slice_count, 1u);

  uint64_t starts[1] = {0};
  uint64_t lengths[1] = {1};
  ASSERT_NO_FATAL_FAILURE(CheckAllocatedSlices(device.get(), starts, lengths, 1));
}

TEST(FakeFVMBlockDeviceTest, ExtendOutOfRange) {
  std::unique_ptr<BlockDevice> device = std::make_unique<FakeFVMBlockDevice>(
      kBlockCountDefault, kBlockSizeDefault, kSliceSizeDefault, kSliceCountDefault);
  EXPECT_EQ(device->VolumeExtend(fvm::kMaxVSlices - 1, 1), ZX_OK);
  EXPECT_EQ(device->VolumeShrink(fvm::kMaxVSlices - 1, 1), ZX_OK);

  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, device->VolumeExtend(fvm::kMaxVSlices, 1));
  EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, device->VolumeShrink(fvm::kMaxVSlices, 1));
}

}  // namespace
}  // namespace block_client
