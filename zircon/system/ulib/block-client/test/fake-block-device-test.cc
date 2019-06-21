// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <block-client/cpp/fake-device.h>

#include <fvm/format.h>
#include <zxtest/zxtest.h>

namespace block_client {
namespace {

constexpr uint64_t kBlockCountDefault = 1024;
constexpr uint32_t kBlockSizeDefault = 512;
constexpr uint64_t kSliceSizeDefault = 1024;
constexpr uint64_t kSliceCountDefault = 128;

TEST(FakeBlockDeviceTest, EmptyDevice) {
    const uint64_t kBlockCount = 0;
    const uint32_t kBlockSize = 0;
    std::unique_ptr<BlockDevice> device =
            std::make_unique<FakeBlockDevice>(kBlockCount, kBlockSize);
    fuchsia_hardware_block_BlockInfo info = {};
    ASSERT_OK(device->BlockGetInfo(&info));
    EXPECT_EQ(kBlockCount, info.block_count);
    EXPECT_EQ(kBlockSize, info.block_size);
}

TEST(FakeBlockDeviceTest, NonEmptyDevice) {
    std::unique_ptr<BlockDevice> device =
            std::make_unique<FakeBlockDevice>(kBlockCountDefault, kBlockSizeDefault);
    fuchsia_hardware_block_BlockInfo info = {};
    ASSERT_OK(device->BlockGetInfo(&info));
    EXPECT_EQ(kBlockCountDefault, info.block_count);
    EXPECT_EQ(kBlockSizeDefault, info.block_size);
}

void CreateAndRegisterVmo(BlockDevice* device, size_t blocks, zx::vmo* vmo,
                          fuchsia_hardware_block_VmoID* vmoid) {
    fuchsia_hardware_block_BlockInfo info = {};
    ASSERT_OK(device->BlockGetInfo(&info));
    zx::vmo dup;
    ASSERT_OK(zx::vmo::create(blocks * info.block_size, 0, vmo));
    ASSERT_OK(vmo->duplicate(ZX_RIGHT_SAME_RIGHTS, &dup));
    ASSERT_OK(device->BlockAttachVmo(std::move(dup), vmoid));
}

TEST(FakeBlockDeviceTest, WriteAndReadUsingFifoTransaction) {
    std::unique_ptr<BlockDevice> device =
            std::make_unique<FakeBlockDevice>(kBlockCountDefault, kBlockSizeDefault);

    const size_t kVmoBlocks = 4;
    zx::vmo vmo;
    fuchsia_hardware_block_VmoID vmoid;
    ASSERT_NO_FAILURES(CreateAndRegisterVmo(device.get(), kVmoBlocks, &vmo, &vmoid));

    // Write some data to the device.
    char src[kVmoBlocks * kBlockSizeDefault];
    memset(src, 'a', sizeof(src));
    ASSERT_OK(vmo.write(src, 0, sizeof(src)));
    block_fifo_request_t request;
    request.opcode = BLOCKIO_WRITE;
    request.vmoid = vmoid.id;
    request.length = kVmoBlocks;
    request.vmo_offset = 0;
    request.dev_offset = 0;
    ASSERT_OK(device->FifoTransaction(&request, 1));

    // Clear out the registered VMO.
    char dst[kVmoBlocks * kBlockSizeDefault];
    static_assert(sizeof(src) == sizeof(dst), "Mismatched input/output buffer size");
    memset(dst, 0, sizeof(dst));
    ASSERT_OK(vmo.write(dst, 0, sizeof(dst)));

    // Read data from the fake back into the registered VMO.
    request.opcode = BLOCKIO_READ;
    request.vmoid = vmoid.id;
    request.length = kVmoBlocks;
    request.vmo_offset = 0;
    request.dev_offset = 0;
    ASSERT_OK(device->FifoTransaction(&request, 1));
    ASSERT_OK(vmo.read(dst, 0 ,sizeof(dst)));
    EXPECT_BYTES_EQ(src, dst, sizeof(src));
}

TEST(FakeBlockDeviceTest, FifoTransactionFlush) {
    std::unique_ptr<BlockDevice> device =
            std::make_unique<FakeBlockDevice>(kBlockCountDefault, kBlockSizeDefault);

    const size_t kVmoBlocks = 1;
    zx::vmo vmo;
    fuchsia_hardware_block_VmoID vmoid;
    ASSERT_NO_FAILURES(CreateAndRegisterVmo(device.get(), kVmoBlocks, &vmo, &vmoid));

    block_fifo_request_t request;
    request.opcode = BLOCKIO_FLUSH;
    request.vmoid = vmoid.id;
    request.length = 0;
    request.vmo_offset = 0;
    request.dev_offset = 0;
    EXPECT_OK(device->FifoTransaction(&request, 1));
}

// Tests that writing followed by a flush acts like a regular write.
TEST(FakeBlockDeviceTest, FifoTransactionWriteThenFlush) {
    std::unique_ptr<BlockDevice> device =
            std::make_unique<FakeBlockDevice>(kBlockCountDefault, kBlockSizeDefault);

    const size_t kVmoBlocks = 1;
    zx::vmo vmo;
    fuchsia_hardware_block_VmoID vmoid;
    ASSERT_NO_FAILURES(CreateAndRegisterVmo(device.get(), kVmoBlocks, &vmo, &vmoid));

    char src[kVmoBlocks * kBlockSizeDefault];
    memset(src, 'a', sizeof(src));
    ASSERT_OK(vmo.write(src, 0, sizeof(src)));

    block_fifo_request_t requests[2];
    requests[0].opcode = BLOCKIO_WRITE;
    requests[0].vmoid = vmoid.id;
    requests[0].length = kVmoBlocks;
    requests[0].vmo_offset = 0;
    requests[0].dev_offset = 0;

    requests[1].opcode = BLOCKIO_FLUSH;
    requests[1].vmoid = vmoid.id;
    requests[1].length = 0;
    requests[1].vmo_offset = 0;
    requests[1].dev_offset = 0;
    EXPECT_OK(device->FifoTransaction(requests, fbl::count_of(requests)));

    char dst[kVmoBlocks * kBlockSizeDefault];
    static_assert(sizeof(src) == sizeof(dst), "Mismatched input/output buffer size");
    memset(dst, 0, sizeof(dst));
    ASSERT_OK(vmo.write(dst, 0, sizeof(dst)));

    block_fifo_request_t request;
    request.opcode = BLOCKIO_READ;
    request.vmoid = vmoid.id;
    request.length = kVmoBlocks;
    request.vmo_offset = 0;
    request.dev_offset = 0;
    ASSERT_OK(device->FifoTransaction(&request, 1));
    ASSERT_OK(vmo.read(dst, 0 ,sizeof(dst)));
    EXPECT_BYTES_EQ(src, dst, sizeof(src));
}

// Tests that flushing followed by a write acts like a regular write.
TEST(FakeBlockDeviceTest, FifoTransactionFlushThenWrite) {
    std::unique_ptr<BlockDevice> device =
            std::make_unique<FakeBlockDevice>(kBlockCountDefault, kBlockSizeDefault);

    const size_t kVmoBlocks = 1;
    zx::vmo vmo;
    fuchsia_hardware_block_VmoID vmoid;
    ASSERT_NO_FAILURES(CreateAndRegisterVmo(device.get(), kVmoBlocks, &vmo, &vmoid));

    char src[kVmoBlocks * kBlockSizeDefault];
    memset(src, 'a', sizeof(src));
    ASSERT_OK(vmo.write(src, 0, sizeof(src)));

    block_fifo_request_t requests[2];
    requests[0].opcode = BLOCKIO_FLUSH;
    requests[0].vmoid = vmoid.id;
    requests[0].length = 0;
    requests[0].vmo_offset = 0;
    requests[0].dev_offset = 0;

    requests[1].opcode = BLOCKIO_WRITE;
    requests[1].vmoid = vmoid.id;
    requests[1].length = kVmoBlocks;
    requests[1].vmo_offset = 0;
    requests[1].dev_offset = 0;

    EXPECT_OK(device->FifoTransaction(requests, fbl::count_of(requests)));

    char dst[kVmoBlocks * kBlockSizeDefault];
    static_assert(sizeof(src) == sizeof(dst), "Mismatched input/output buffer size");
    memset(dst, 0, sizeof(dst));
    ASSERT_OK(vmo.write(dst, 0, sizeof(dst)));

    block_fifo_request_t request;
    request.opcode = BLOCKIO_READ;
    request.vmoid = vmoid.id;
    request.length = kVmoBlocks;
    request.vmo_offset = 0;
    request.dev_offset = 0;
    ASSERT_OK(device->FifoTransaction(&request, 1));
    ASSERT_OK(vmo.read(dst, 0 ,sizeof(dst)));
    EXPECT_BYTES_EQ(src, dst, sizeof(src));
}

TEST(FakeBlockDeviceTest, FifoTransactionClose) {
    auto fake_device = std::make_unique<FakeBlockDevice>(kBlockCountDefault, kBlockSizeDefault);
    auto device = reinterpret_cast<BlockDevice*>(fake_device.get());

    const size_t kVmoBlocks = 1;
    zx::vmo vmo;
    fuchsia_hardware_block_VmoID vmoid;
    ASSERT_NO_FAILURES(CreateAndRegisterVmo(device, kVmoBlocks, &vmo, &vmoid));

    block_fifo_request_t request;
    request.opcode = BLOCKIO_CLOSE_VMO;
    request.vmoid = vmoid.id;
    request.length = 0;
    request.vmo_offset = 0;
    request.dev_offset = 0;

    EXPECT_TRUE(fake_device->IsRegistered(vmoid.id));
    EXPECT_OK(device->FifoTransaction(&request, 1));
    EXPECT_FALSE(fake_device->IsRegistered(vmoid.id));
}

TEST(FakeBlockDeviceTest, FifoTransactionUnsupportedTrim) {
    std::unique_ptr<BlockDevice> device =
            std::make_unique<FakeBlockDevice>(kBlockCountDefault, kBlockSizeDefault);

    const size_t kVmoBlocks = 1;
    zx::vmo vmo;
    fuchsia_hardware_block_VmoID vmoid;
    ASSERT_NO_FAILURES(CreateAndRegisterVmo(device.get(), kVmoBlocks, &vmo, &vmoid));

    block_fifo_request_t request;
    request.opcode = BLOCKIO_TRIM;
    request.vmoid = vmoid.id;
    request.length = kVmoBlocks;
    request.vmo_offset = 0;
    request.dev_offset = 0;
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, device->FifoTransaction(&request, 1));
}

TEST(FakeFVMBlockDeviceTest, QueryVolume) {
    std::unique_ptr<BlockDevice> device =
            std::make_unique<FakeFVMBlockDevice>(kBlockCountDefault, kBlockSizeDefault,
                                                 kSliceSizeDefault, kSliceCountDefault);
    {
        fuchsia_hardware_block_BlockInfo info = {};
        ASSERT_OK(device->BlockGetInfo(&info));
        EXPECT_EQ(kBlockCountDefault, info.block_count);
        EXPECT_EQ(kBlockSizeDefault, info.block_size);
    }

    {
        fuchsia_hardware_block_volume_VolumeInfo info = {};
        ASSERT_OK(device->VolumeQuery(&info));
        EXPECT_EQ(kSliceSizeDefault, info.slice_size);
        EXPECT_EQ(1, info.pslice_allocated_count);
    }
}

TEST(FakeFVMBlockDeviceTest, QuerySlices) {
    std::unique_ptr<BlockDevice> device =
            std::make_unique<FakeFVMBlockDevice>(kBlockCountDefault, kBlockSizeDefault,
                                                 kSliceSizeDefault, kSliceCountDefault);
    uint64_t slice_start = 0;
    size_t slice_count = 1;
    fuchsia_hardware_block_volume_VsliceRange ranges;
    size_t actual_ranges = 0;
    ASSERT_EQ(ZX_OK,
              device->VolumeQuerySlices(&slice_start, slice_count, &ranges, &actual_ranges));
    ASSERT_EQ(1, actual_ranges);
    EXPECT_TRUE(ranges.allocated);
    EXPECT_EQ(1, ranges.count);

    slice_start = 1;
    actual_ranges = 0;
    ASSERT_EQ(ZX_OK,
              device->VolumeQuerySlices(&slice_start, slice_count, &ranges, &actual_ranges));
    ASSERT_EQ(1, actual_ranges);
    EXPECT_FALSE(ranges.allocated);
    EXPECT_EQ(fvm::kMaxVSlices - 1, ranges.count);

    slice_start = fvm::kMaxVSlices;
    actual_ranges = 0;
    ASSERT_EQ(ZX_ERR_OUT_OF_RANGE,
              device->VolumeQuerySlices(&slice_start, slice_count, &ranges, &actual_ranges));
    ASSERT_EQ(0, actual_ranges);
}

void CheckAllocatedSlices(BlockDevice* device, const uint64_t* starts, const uint64_t* lengths,
                          size_t slices_count) {
    fuchsia_hardware_block_volume_VsliceRange ranges[slices_count];
    size_t actual_ranges = 0;
    ASSERT_EQ(ZX_OK,
              device->VolumeQuerySlices(starts, slices_count, ranges, &actual_ranges));
    ASSERT_EQ(slices_count, actual_ranges);
    for (size_t i = 0; i < slices_count; i++) {
        EXPECT_TRUE(ranges[i].allocated);
        EXPECT_EQ(lengths[i], ranges[i].count);
    }
}

TEST(FakeFVMBlockDeviceTest, ExtendNoOp) {
    std::unique_ptr<BlockDevice> device =
            std::make_unique<FakeFVMBlockDevice>(kBlockCountDefault, kBlockSizeDefault,
                                                 kSliceSizeDefault, kSliceCountDefault);

    fuchsia_hardware_block_volume_VolumeInfo info = {};
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(1, info.pslice_allocated_count);

    ASSERT_OK(device->VolumeExtend(0, 0));
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(1, info.pslice_allocated_count);

    uint64_t starts = 0;
    uint64_t lengths = 1;
    ASSERT_NO_FAILURES(CheckAllocatedSlices(device.get(), &starts, &lengths, 1));
}

TEST(FakeFVMBlockDeviceTest, ExtendOverlappingSameStart) {
    std::unique_ptr<BlockDevice> device =
            std::make_unique<FakeFVMBlockDevice>(kBlockCountDefault, kBlockSizeDefault,
                                                 kSliceSizeDefault, kSliceCountDefault);

    fuchsia_hardware_block_volume_VolumeInfo info = {};
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(1, info.pslice_allocated_count);

    ASSERT_OK(device->VolumeExtend(0, 2));
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(2, info.pslice_allocated_count);

    uint64_t starts = 0;
    uint64_t lengths = 2;
    ASSERT_NO_FAILURES(CheckAllocatedSlices(device.get(), &starts, &lengths, 1));
}

TEST(FakeFVMBlockDeviceTest, ExtendOverlappingDifferentStart) {
    std::unique_ptr<BlockDevice> device =
            std::make_unique<FakeFVMBlockDevice>(kBlockCountDefault, kBlockSizeDefault,
                                                 kSliceSizeDefault, kSliceCountDefault);

    fuchsia_hardware_block_volume_VolumeInfo info = {};
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(1, info.pslice_allocated_count);

    ASSERT_OK(device->VolumeExtend(1, 2));
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(3, info.pslice_allocated_count);

    uint64_t starts = 0;
    uint64_t lengths = 3;
    ASSERT_NO_FAILURES(CheckAllocatedSlices(device.get(), &starts, &lengths, 1));
}

TEST(FakeFVMBlockDeviceTest, ExtendNonOverlapping) {
    std::unique_ptr<BlockDevice> device =
            std::make_unique<FakeFVMBlockDevice>(kBlockCountDefault, kBlockSizeDefault,
                                                 kSliceSizeDefault, kSliceCountDefault);

    fuchsia_hardware_block_volume_VolumeInfo info = {};
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(1, info.pslice_allocated_count);

    ASSERT_OK(device->VolumeExtend(2, 2));
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(3, info.pslice_allocated_count);

    uint64_t starts[2] = { 0, 2 };
    uint64_t lengths[2] = { 1, 2 };
    ASSERT_NO_FAILURES(CheckAllocatedSlices(device.get(), starts, lengths, 2));
}

TEST(FakeFVMBlockDeviceTest, ShrinkNoOp) {
    std::unique_ptr<BlockDevice> device =
            std::make_unique<FakeFVMBlockDevice>(kBlockCountDefault, kBlockSizeDefault,
                                                 kSliceSizeDefault, kSliceCountDefault);

    fuchsia_hardware_block_volume_VolumeInfo info = {};
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(1, info.pslice_allocated_count);

    ASSERT_OK(device->VolumeShrink(0, 0));
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(1, info.pslice_allocated_count);
}

TEST(FakeFVMBlockDeviceTest, ShrinkInvalid) {
    std::unique_ptr<BlockDevice> device =
            std::make_unique<FakeFVMBlockDevice>(kBlockCountDefault, kBlockSizeDefault,
                                                 kSliceSizeDefault, kSliceCountDefault);

    fuchsia_hardware_block_volume_VolumeInfo info = {};
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(1, info.pslice_allocated_count);

    ASSERT_EQ(ZX_ERR_INVALID_ARGS, device->VolumeShrink(100, 5));
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(1, info.pslice_allocated_count);
}

// [0, 0) -> Extend
// [0, 11) -> Shrink
// [0, 0)
TEST(FakeFVMBlockDeviceTest, ExtendThenShrinkSubSection) {
    std::unique_ptr<BlockDevice> device =
            std::make_unique<FakeFVMBlockDevice>(kBlockCountDefault, kBlockSizeDefault,
                                                 kSliceSizeDefault, kSliceCountDefault);

    fuchsia_hardware_block_volume_VolumeInfo info = {};
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(1, info.pslice_allocated_count);

    ASSERT_OK(device->VolumeExtend(1, 10));
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(11, info.pslice_allocated_count);

    ASSERT_OK(device->VolumeShrink(1, 10));
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(1, info.pslice_allocated_count);

    uint64_t starts[1] = { 0 };
    uint64_t lengths[1] = { 1 };
    ASSERT_NO_FAILURES(CheckAllocatedSlices(device.get(), starts, lengths, 1));
}

// [0, 0) -> Extend
// [0, 0) + [5, 15) -> Shrink
// [0, 0) + [6, 15) -> Shrink
// [0, 0) + [6, 14)
TEST(FakeFVMBlockDeviceTest, ExtendThenShrinkPartialOverlap) {
    std::unique_ptr<BlockDevice> device =
            std::make_unique<FakeFVMBlockDevice>(kBlockCountDefault, kBlockSizeDefault,
                                                 kSliceSizeDefault, kSliceCountDefault);

    fuchsia_hardware_block_volume_VolumeInfo info = {};
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(1, info.pslice_allocated_count);

    ASSERT_OK(device->VolumeExtend(5, 10));
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(11, info.pslice_allocated_count);

    // One slice overlaps, one doesn't.
    ASSERT_OK(device->VolumeShrink(4, 2));
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(10, info.pslice_allocated_count);

    // One slice overlaps, one doesn't.
    ASSERT_OK(device->VolumeShrink(14, 2));
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(9, info.pslice_allocated_count);

    uint64_t starts[2] = { 0, 6 };
    uint64_t lengths[2] = { 1, 8 };
    ASSERT_NO_FAILURES(CheckAllocatedSlices(device.get(), starts, lengths, 2));
}

// [0, 0) -> Extend
// [0, 0) + [5, 15) -> Shrink
// [0, 0)
TEST(FakeFVMBlockDeviceTest, ExtendThenShrinkTotal) {
    std::unique_ptr<BlockDevice> device =
            std::make_unique<FakeFVMBlockDevice>(kBlockCountDefault, kBlockSizeDefault,
                                                 kSliceSizeDefault, kSliceCountDefault);

    fuchsia_hardware_block_volume_VolumeInfo info = {};
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(1, info.pslice_allocated_count);

    ASSERT_OK(device->VolumeExtend(5, 10));
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(11, info.pslice_allocated_count);

    ASSERT_OK(device->VolumeShrink(5, 10));
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(1, info.pslice_allocated_count);

    uint64_t starts[1] = { 0 };
    uint64_t lengths[1] = { 1 };
    ASSERT_NO_FAILURES(CheckAllocatedSlices(device.get(), starts, lengths, 1));
}

// [0, 0) -> Extend
// [0, 0) + [5, 15) -> Shrink
// [0, 0) + [5, 6) + [9, 15)
TEST(FakeFVMBlockDeviceTest, ExtendThenShrinkToSplit) {
    std::unique_ptr<BlockDevice> device =
            std::make_unique<FakeFVMBlockDevice>(kBlockCountDefault, kBlockSizeDefault,
                                                 kSliceSizeDefault, kSliceCountDefault);

    fuchsia_hardware_block_volume_VolumeInfo info = {};
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(1, info.pslice_allocated_count);

    ASSERT_OK(device->VolumeExtend(5, 10));
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(11, info.pslice_allocated_count);

    ASSERT_OK(device->VolumeShrink(7, 2));
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(9, info.pslice_allocated_count);

    uint64_t starts[3] = { 0, 5, 9 };
    uint64_t lengths[3] = { 1, 2, 6 };
    ASSERT_NO_FAILURES(CheckAllocatedSlices(device.get(), starts, lengths, 3));
}

// [0, 0) -> Extend
// [0, 10) -> Extend (overallocate)
// [0, 10) -> Shrink
// [0, 9) -> Extend
// [0, 9)
TEST(FakeFVMBlockDeviceTest, OverallocateSlices) {
    const uint64_t kSliceCapacity = 10;
    std::unique_ptr<BlockDevice> device =
            std::make_unique<FakeFVMBlockDevice>(kBlockCountDefault, kBlockSizeDefault,
                                                 kSliceSizeDefault, kSliceCapacity);

    fuchsia_hardware_block_volume_VolumeInfo info = {};
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(1, info.pslice_allocated_count);
    EXPECT_EQ(kSliceCapacity, info.pslice_total_count);

    // Allocate all slices.
    ASSERT_OK(device->VolumeExtend(1, info.pslice_total_count - info.pslice_allocated_count));
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(kSliceCapacity, info.pslice_allocated_count);

    // We cannot allocate more slices without remaining space.
    ASSERT_EQ(ZX_ERR_NO_SPACE, device->VolumeExtend(kSliceCapacity, 1));

    // However, if we shrink an earlier slice, we can re-allocate.
    ASSERT_OK(device->VolumeShrink(kSliceCapacity - 1, 1));
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(kSliceCapacity - 1, info.pslice_allocated_count);
    ASSERT_OK(device->VolumeExtend(kSliceCapacity, 1));
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(kSliceCapacity, info.pslice_allocated_count);

    uint64_t starts[2] = { 0, kSliceCapacity };
    uint64_t lengths[2] = { kSliceCapacity - 1, 1 };
    ASSERT_NO_FAILURES(CheckAllocatedSlices(device.get(), starts, lengths, 2));
}

// [0, 0) -> Extend (overallocate)
// [0, 0)
TEST(FakeFVMBlockDeviceTest, PartialOverallocateSlices) {
    const uint64_t kSliceCapacity = 10;
    std::unique_ptr<BlockDevice> device =
            std::make_unique<FakeFVMBlockDevice>(kBlockCountDefault, kBlockSizeDefault,
                                                 kSliceSizeDefault, kSliceCapacity);

    fuchsia_hardware_block_volume_VolumeInfo info = {};
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(1, info.pslice_allocated_count);
    EXPECT_EQ(kSliceCapacity, info.pslice_total_count);

    // Allocating too many slices up front should not allocate any slices.
    ASSERT_EQ(ZX_ERR_NO_SPACE, device->VolumeExtend(1, info.pslice_total_count));
    ASSERT_OK(device->VolumeQuery(&info));
    EXPECT_EQ(1, info.pslice_allocated_count);

    uint64_t starts[1] = { 0 };
    uint64_t lengths[1] = { 1 };
    ASSERT_NO_FAILURES(CheckAllocatedSlices(device.get(), starts, lengths, 1));
}

TEST(FakeFVMBlockDeviceTest, ExtendOutOfRange) {
    std::unique_ptr<BlockDevice> device =
            std::make_unique<FakeFVMBlockDevice>(kBlockCountDefault, kBlockSizeDefault,
                                                 kSliceSizeDefault, kSliceCountDefault);
    EXPECT_OK(device->VolumeExtend(fvm::kMaxVSlices - 1, 1));
    EXPECT_OK(device->VolumeShrink(fvm::kMaxVSlices - 1, 1));

    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, device->VolumeExtend(fvm::kMaxVSlices, 1));
    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, device->VolumeShrink(fvm::kMaxVSlices, 1));
}

} // namespace
} // namespace block_client
