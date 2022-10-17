// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/fvm/client.h"

#include <zxtest/zxtest.h>

#include "src/lib/storage/block_client/cpp/block_device.h"

namespace fvm {
namespace {

using block_client::BlockDevice;

class MockDeviceBase : public BlockDevice {
 public:
  virtual ~MockDeviceBase() = default;
  zx_status_t FifoTransaction(block_fifo_request_t* requests, size_t count) final {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx::result<std::string> GetDevicePath() const final { return zx::error(ZX_ERR_NOT_SUPPORTED); }
  zx_status_t BlockGetInfo(fuchsia_hardware_block_BlockInfo* out_info) const final {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out_vmoid) final {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t VolumeGetInfo(fuchsia_hardware_block_volume_VolumeManagerInfo* out_manager,
                            fuchsia_hardware_block_volume_VolumeInfo* out_volume) const final {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t VolumeQuerySlices(const uint64_t* slices, size_t slices_count,
                                fuchsia_hardware_block_volume_VsliceRange* out_ranges,
                                size_t* out_ranges_count) const override {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t VolumeExtend(uint64_t offset, uint64_t length) final { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t VolumeShrink(uint64_t offset, uint64_t length) override {
    return ZX_ERR_NOT_SUPPORTED;
  }
};

TEST(FvmClientTest, ResetSlicesNotSupported) {
  MockDeviceBase device;
  ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, ResetAllSlices(&device));
}

class MockBadDevice : public MockDeviceBase {
 public:
  zx_status_t VolumeQuerySlices(const uint64_t* slices, size_t slices_count,
                                fuchsia_hardware_block_volume_VsliceRange* out_ranges,
                                size_t* out_ranges_count) const override {
    *out_ranges_count = 0;
    return ZX_OK;
  }
  zx_status_t VolumeShrink(uint64_t offset, uint64_t length) override { return ZX_OK; }
};

TEST(FvmClientTest, ResetSlicesBadDevice) {
  MockBadDevice device;
  ASSERT_EQ(ZX_ERR_IO, ResetAllSlices(&device));
}

//  [1, 10]: Allocated
class MockOneSliceRangeDevice : public MockDeviceBase {
 public:
  zx_status_t VolumeQuerySlices(const uint64_t* slices, size_t slices_count,
                                fuchsia_hardware_block_volume_VsliceRange* out_ranges,
                                size_t* out_ranges_count) const override {
    EXPECT_EQ(1, slices_count);
    if (slices[0] == 1) {
      out_ranges[0].count = 10;
      out_ranges[0].allocated = true;
      *out_ranges_count = 1;
      return ZX_OK;
    } else {
      return ZX_ERR_OUT_OF_RANGE;
    }
  }

  zx_status_t VolumeShrink(uint64_t offset, uint64_t length) override {
    EXPECT_EQ(1, offset);
    EXPECT_EQ(10, length);
    EXPECT_FALSE(shrink_called_);
    shrink_called_ = true;
    return ZX_OK;
  }

  bool shrink_called_ = false;
};

TEST(FvmClientTest, ResetSlicesOneSliceRange) {
  MockOneSliceRangeDevice device;
  EXPECT_OK(ResetAllSlices(&device));
  EXPECT_TRUE(device.shrink_called_);
}

//  [1, 10]: Allocated
// [11, 20]: Not Allocated
// [21, 30]: Allocated
class MockManySliceRangesDevice : public MockDeviceBase {
 public:
  zx_status_t VolumeQuerySlices(const uint64_t* slices, size_t slices_count,
                                fuchsia_hardware_block_volume_VsliceRange* out_ranges,
                                size_t* out_ranges_count) const override {
    EXPECT_EQ(1, slices_count);
    switch (slices[0]) {
      case 1:
        out_ranges[0].count = 10;
        out_ranges[0].allocated = true;
        *out_ranges_count = 1;
        return ZX_OK;
      case 11:
        out_ranges[0].count = 10;
        out_ranges[0].allocated = false;
        *out_ranges_count = 1;
        return ZX_OK;
      case 21:
        out_ranges[0].count = 10;
        out_ranges[0].allocated = true;
        *out_ranges_count = 1;
        return ZX_OK;
      default:
        return ZX_ERR_OUT_OF_RANGE;
    }
  }

  zx_status_t VolumeShrink(uint64_t offset, uint64_t length) override {
    switch (offset) {
      case 1:
        EXPECT_EQ(10, length);
        EXPECT_FALSE(shrink_called_[0]);
        shrink_called_[0] = true;
        return ZX_OK;
      case 21:
        EXPECT_EQ(10, length);
        EXPECT_FALSE(shrink_called_[1]);
        shrink_called_[1] = true;
        return ZX_OK;
      default:
        return ZX_ERR_IO;
    }
  }

  bool shrink_called_[2] = {};
};

TEST(FvmClientTest, ResetSlicesManySliceRanges) {
  MockManySliceRangesDevice device;
  EXPECT_OK(ResetAllSlices(&device));
  EXPECT_TRUE(device.shrink_called_[0]);
  EXPECT_TRUE(device.shrink_called_[1]);
}

}  // namespace
}  // namespace fvm
