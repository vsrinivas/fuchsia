// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../dma-mgr.h"

#include <fcntl.h>
#include <fuchsia/camera/c/fidl.h>
#include <fuchsia/sysmem/c/fidl.h>
#include <lib/fake-bti/bti.h>
#include <lib/syslog/global.h>
#include <stdlib.h>
#include <unistd.h>

#include <memory>
#include <vector>

#include <ddk/debug.h>
#include <zxtest/zxtest.h>

#include "lib/fit/function.h"
#include "lib/mmio/mmio.h"
#include "lib/zx/vmo.h"
#include "src/camera/drivers/test_utils/fake-buffer-collection.h"

namespace camera {
namespace {
constexpr uint32_t kMagicDmaAddressValue = 0x1337BEEF;

using Stream = DmaManager::Stream;

// Integration test for the driver defined in zircon/system/dev/camera/arm-isp.
class DmaMgrTest : public zxtest::Test {
 protected:
  static constexpr uint32_t kFullResWidth = 1080;
  static constexpr uint32_t kFullResHeight = 764;
  static constexpr uint32_t kFullResNumberOfBuffers = 8;
  static constexpr uint32_t kDownscaledWidth = 1080;
  static constexpr uint32_t kDownscaledHeight = 764;
  static constexpr uint32_t kDownscaledNumberOfBuffers = 8;
  static constexpr uint32_t kLocalBufferSize = (0x18e88 + 0x4000);
  static constexpr uint32_t kPixelFormatType = fuchsia_sysmem_PixelFormatType_NV12;
  void SetUp() override {
    mmio_buffer_t local_mmio_buffer;
    local_mmio_buffer.vaddr = local_mmio_buffer_;
    local_mmio_buffer.size = kLocalBufferSize;
    local_mmio_buffer.vmo = ZX_HANDLE_INVALID;
    local_mmio_buffer.offset = 0;

    ASSERT_OK(fake_bti_create(bti_.reset_and_get_address()));
    ASSERT_OK(DmaManager::Create(bti_, ddk::MmioView(local_mmio_buffer, 0),
                                 DmaManager::Stream::FullResolution, &full_resolution_dma_));
    ASSERT_OK(DmaManager::Create(bti_, ddk::MmioView(local_mmio_buffer, 0),
                                 DmaManager::Stream::Downscaled, &downscaled_dma_));

    ASSERT_OK(camera::GetImageFormat(full_resolution_image_format_, kPixelFormatType, kFullResWidth,
                                     kFullResHeight));
    ASSERT_OK(camera::GetImageFormat(downscaled_image_format_, kPixelFormatType, kDownscaledWidth,
                                     kDownscaledHeight));

    zx_status_t status = CreateContiguousBufferCollectionInfo(full_resolution_buffer_collection_,
                                                              full_resolution_image_format_,
                                                              bti_.get(), kFullResNumberOfBuffers);
    ASSERT_OK(status);
    status = CreateContiguousBufferCollectionInfo(downscaled_buffer_collection_,
                                                  downscaled_image_format_, bti_.get(),
                                                  kDownscaledNumberOfBuffers);
    mmio_view_.emplace(local_mmio_buffer, 0);
    ASSERT_OK(status);
  }

  void FullResCallback(frame_available_info info) { full_resolution_callbacks_.push_back(info); }

  void DownScaledCallback(frame_available_info info) { downscaled_callbacks_.push_back(info); }

  bool CheckWriteEnabled(Stream type) {
    if (type == Stream::FullResolution) {
      return ping::FullResolution::Primary::DmaWriter_Misc::Get()
          .ReadFrom(&(*mmio_view_))
          .frame_write_on();
    }
    return ping::DownScaled::Primary::DmaWriter_Misc::Get()
        .ReadFrom(&(*mmio_view_))
        .frame_write_on();
  }

  // Checks that the dma write address is not the kMagicDmaAddressValue.
  // Used to verify that the dma manager assigned a new write address
  void CheckDmaWroteAddress(Stream type) {
    if (type == Stream::FullResolution) {
      EXPECT_NE(ping::FullResolution::Primary::DmaWriter_Bank0Base::Get()
                    .ReadFrom(&(*mmio_view_))
                    .value(),
                kMagicDmaAddressValue);
      EXPECT_NE(
          ping::FullResolution::Uv::DmaWriter_Bank0Base::Get().ReadFrom(&(*mmio_view_)).value(),
          kMagicDmaAddressValue);
    } else {
      EXPECT_NE(
          ping::DownScaled::Primary::DmaWriter_Bank0Base::Get().ReadFrom(&(*mmio_view_)).value(),
          kMagicDmaAddressValue);
      EXPECT_NE(ping::DownScaled::Uv::DmaWriter_Bank0Base::Get().ReadFrom(&(*mmio_view_)).value(),
                kMagicDmaAddressValue);
    }
  }
  // Checks that the dma write address is the kMagicDmaAddressValue.
  // Used to verify that the dma manager did not assign a new write address
  void CheckNoDmaWriteAddress(Stream type) {
    if (type == Stream::FullResolution) {
      EXPECT_EQ(ping::FullResolution::Primary::DmaWriter_Bank0Base::Get()
                    .ReadFrom(&(*mmio_view_))
                    .value(),
                kMagicDmaAddressValue);
      EXPECT_EQ(
          ping::FullResolution::Uv::DmaWriter_Bank0Base::Get().ReadFrom(&(*mmio_view_)).value(),
          kMagicDmaAddressValue);
    } else {
      EXPECT_EQ(
          ping::DownScaled::Primary::DmaWriter_Bank0Base::Get().ReadFrom(&(*mmio_view_)).value(),
          kMagicDmaAddressValue);
      EXPECT_EQ(ping::DownScaled::Uv::DmaWriter_Bank0Base::Get().ReadFrom(&(*mmio_view_)).value(),
                kMagicDmaAddressValue);
    }
  }

  // Sets the write addresses to kMagicDmaAddressValue, which is different
  // from what they should ever be set to. This allows us to detect when
  // the register has been written.
  void SetMagicWriteAddresses() {
    ping::FullResolution::Primary::DmaWriter_Bank0Base::Get()
        .FromValue(0)
        .set_value(kMagicDmaAddressValue)
        .WriteTo(&(*mmio_view_));
    ping::DownScaled::Primary::DmaWriter_Bank0Base::Get()
        .FromValue(0)
        .set_value(kMagicDmaAddressValue)
        .WriteTo(&(*mmio_view_));
    ping::FullResolution::Uv::DmaWriter_Bank0Base::Get()
        .FromValue(0)
        .set_value(kMagicDmaAddressValue)
        .WriteTo(&(*mmio_view_));
    ping::DownScaled::Uv::DmaWriter_Bank0Base::Get()
        .FromValue(0)
        .set_value(kMagicDmaAddressValue)
        .WriteTo(&(*mmio_view_));
  }

  void ConnectToStreams() {
    zx_status_t status = full_resolution_dma_->Configure(
        full_resolution_buffer_collection_, full_resolution_image_format_,
        fit::bind_member(this, &DmaMgrTest::FullResCallback));
    EXPECT_OK(status);
    status = downscaled_dma_->Configure(downscaled_buffer_collection_, downscaled_image_format_,
                                        fit::bind_member(this, &DmaMgrTest::DownScaledCallback));
    EXPECT_OK(status);
    full_resolution_dma_->Enable();
    downscaled_dma_->Enable();
  }

  void TearDown() override {
    ASSERT_OK(camera::DestroyContiguousBufferCollection(full_resolution_buffer_collection_));
    ASSERT_OK(camera::DestroyContiguousBufferCollection(downscaled_buffer_collection_));
  }

  char local_mmio_buffer_[kLocalBufferSize];
  zx::bti bti_;
  std::optional<ddk::MmioView> mmio_view_;
  std::unique_ptr<camera::DmaManager> full_resolution_dma_;
  std::unique_ptr<camera::DmaManager> downscaled_dma_;
  fuchsia_sysmem_BufferCollectionInfo_2 full_resolution_buffer_collection_;
  fuchsia_sysmem_BufferCollectionInfo_2 downscaled_buffer_collection_;
  fuchsia_sysmem_ImageFormat_2 full_resolution_image_format_;
  fuchsia_sysmem_ImageFormat_2 downscaled_image_format_;
  std::vector<frame_available_info> full_resolution_callbacks_;
  std::vector<frame_available_info> downscaled_callbacks_;
};

TEST_F(DmaMgrTest, EnableDeathTest) {
  // We should die because we don't have a callback registered:
  ASSERT_DEATH(([this]() { full_resolution_dma_->Enable(); }));
  // But since we are not enabled, OnNewFrame does nothing.
  ASSERT_NO_DEATH(([this]() { full_resolution_dma_->OnNewFrame(); }));
  ConnectToStreams();
  ASSERT_NO_DEATH(([this]() { full_resolution_dma_->Enable(); }));
}

TEST_F(DmaMgrTest, BasicConnectionTest) {
  EXPECT_FALSE(CheckWriteEnabled(Stream::Downscaled));
  EXPECT_FALSE(CheckWriteEnabled(Stream::FullResolution));
  ConnectToStreams();

  full_resolution_dma_->OnNewFrame();
  // Test that the outputs are enabled:
  EXPECT_FALSE(CheckWriteEnabled(Stream::Downscaled));
  EXPECT_TRUE(CheckWriteEnabled(Stream::FullResolution));
  EXPECT_EQ(full_resolution_callbacks_.size(), 0);
  full_resolution_dma_->OnNewFrame();
  EXPECT_EQ(full_resolution_callbacks_.size(), 1);
}

TEST_F(DmaMgrTest, EnableCallbacksTest) {
  ConnectToStreams();
  full_resolution_dma_->Disable();

  full_resolution_dma_->OnNewFrame();
  // Test that the outputs are not enabled:
  EXPECT_FALSE(CheckWriteEnabled(Stream::FullResolution));
  EXPECT_EQ(full_resolution_callbacks_.size(), 0);
  full_resolution_dma_->OnNewFrame();
  EXPECT_EQ(full_resolution_callbacks_.size(), 0);

  full_resolution_dma_->Enable();
  full_resolution_dma_->OnNewFrame();
  // Test that the outputs are enabled:
  EXPECT_TRUE(CheckWriteEnabled(Stream::FullResolution));
  full_resolution_dma_->OnNewFrame();
  EXPECT_EQ(full_resolution_callbacks_.size(), 1);
}

// Make sure a new address is written to the dma frame every time we call
// OnNewFrame:
TEST_F(DmaMgrTest, NewAddressTest) {
  ConnectToStreams();
  SetMagicWriteAddresses();
  // Make sure we are not writing the other stream:
  full_resolution_dma_->OnNewFrame();
  CheckNoDmaWriteAddress(Stream::Downscaled);
  CheckDmaWroteAddress(Stream::FullResolution);
  downscaled_dma_->OnNewFrame();
  CheckDmaWroteAddress(Stream::Downscaled);
  SetMagicWriteAddresses();
  downscaled_dma_->OnNewFrame();
  CheckDmaWroteAddress(Stream::Downscaled);
  CheckNoDmaWriteAddress(Stream::FullResolution);
}

// Test the flow of getting new frames, releasing them
TEST_F(DmaMgrTest, RunOutOfBuffers) {
  ConnectToStreams();
  // Test just write locking:
  for (uint32_t i = 0; i < kFullResNumberOfBuffers; ++i) {
    SetMagicWriteAddresses();
    full_resolution_dma_->LoadNewFrame();
    EXPECT_TRUE(CheckWriteEnabled(Stream::FullResolution));
    CheckDmaWroteAddress(Stream::FullResolution);
    EXPECT_EQ(full_resolution_callbacks_.size(), 0);
  }
  // Now that our buffer is full, we won't be getting any frames.
  // We should get a callback instead, saying out of buffers.
  for (uint32_t i = 0; i < kFullResNumberOfBuffers; ++i) {
    SetMagicWriteAddresses();
    full_resolution_dma_->LoadNewFrame();
    EXPECT_FALSE(CheckWriteEnabled(Stream::FullResolution));
    CheckNoDmaWriteAddress(Stream::FullResolution);
    EXPECT_EQ(full_resolution_callbacks_.size(), 0);  // Loading frames does not trigger a callback
  }
  full_resolution_callbacks_.clear();
  // Now mark them all written.  This will send out notices of okay frames,
  // but all of the frames we are trying to write to are gone, so we are filling
  // the queue with unwritten frames.  This sounds odd, but the ISP only has 2
  // frames that it write-locks at a time, so the client would get the notification pretty soon.
  for (uint32_t i = 0; i < kFullResNumberOfBuffers; ++i) {
    SetMagicWriteAddresses();
    full_resolution_dma_->OnNewFrame();
    CheckNoDmaWriteAddress(Stream::FullResolution);
    EXPECT_EQ(full_resolution_callbacks_.size(), i + 1);
    EXPECT_EQ(full_resolution_callbacks_.back().frame_status, fuchsia_camera_FrameStatus_OK);
  }
  full_resolution_callbacks_.clear();
  // Now we should still not be able to get frames:
  for (uint32_t i = 0; i < kFullResNumberOfBuffers; ++i) {
    SetMagicWriteAddresses();
    full_resolution_dma_->OnNewFrame();
    EXPECT_FALSE(CheckWriteEnabled(Stream::FullResolution));
    CheckNoDmaWriteAddress(Stream::FullResolution);
    EXPECT_EQ(full_resolution_callbacks_.size(), i + 1);
    EXPECT_EQ(full_resolution_callbacks_.back().frame_status,
              fuchsia_camera_FrameStatus_ERROR_BUFFER_FULL);
  }
  // Now release buffers:
  for (uint32_t i = 0; i < kFullResNumberOfBuffers; ++i) {
    EXPECT_OK(full_resolution_dma_->ReleaseFrame(i));
  }
  // We should be able to get frames again:
  SetMagicWriteAddresses();
  full_resolution_dma_->OnNewFrame();
  EXPECT_TRUE(CheckWriteEnabled(Stream::FullResolution));
  CheckDmaWroteAddress(Stream::FullResolution);
}

// Make sure we can switch the dma manager to a different BufferCollection:
TEST_F(DmaMgrTest, MultipleConfigureCalls) {
  ConnectToStreams();
  // Put downscaled in a write lock state
  downscaled_dma_->OnNewFrame();

  // Read lock one of the full_res frames:
  full_resolution_dma_->LoadNewFrame();
  full_resolution_dma_->OnNewFrame();

  // Now connect the dmamgr to a "different" set of buffers.
  // DmaMgr cannot tell the difference between vmos, so we can just pass in the
  // same ones.
  ConnectToStreams();

  // Releasing frames should also fail:
  ASSERT_EQ(full_resolution_callbacks_.size(), 1);
  ASSERT_EQ(full_resolution_callbacks_.back().frame_status, fuchsia_camera_FrameStatus_OK);
  EXPECT_NOT_OK(full_resolution_dma_->ReleaseFrame(full_resolution_callbacks_.back().buffer_id));

  // But future operations will still work:
  full_resolution_callbacks_.clear();
  SetMagicWriteAddresses();
  full_resolution_dma_->OnNewFrame();
  // Make sure we are writing, and that we gave a valid address to the dma
  EXPECT_TRUE(CheckWriteEnabled(Stream::FullResolution));
  CheckDmaWroteAddress(Stream::FullResolution);
  // Make sure we can mark the frame written.
  ASSERT_NO_DEATH(([this]() { full_resolution_dma_->OnNewFrame(); }));
  // Make sure we can release the frame.
  ASSERT_EQ(full_resolution_callbacks_.size(), 1);
  ASSERT_EQ(full_resolution_callbacks_.back().frame_status, fuchsia_camera_FrameStatus_OK);
  EXPECT_OK(full_resolution_dma_->ReleaseFrame(full_resolution_callbacks_.back().buffer_id));
}

// Make sure callbacks can call back into the class.
TEST_F(DmaMgrTest, CallbackReentrancy) {
  uint32_t buffer_id = kFullResNumberOfBuffers;
  zx_status_t status = full_resolution_dma_->Configure(
      full_resolution_buffer_collection_, full_resolution_image_format_,
      [this, &buffer_id](frame_available_info info) {
        buffer_id = info.buffer_id;
        full_resolution_dma_->Disable();
        ASSERT_FALSE(full_resolution_dma_->enabled());
      });
  EXPECT_OK(status);
  full_resolution_dma_->Enable();
  ASSERT_TRUE(full_resolution_dma_->enabled());
  full_resolution_dma_->OnNewFrame();
  EXPECT_TRUE(CheckWriteEnabled(Stream::FullResolution));
  ASSERT_NO_DEATH([this]() { full_resolution_dma_->OnNewFrame(); });
  ASSERT_NE(buffer_id, kFullResNumberOfBuffers);
  full_resolution_dma_->ReleaseFrame(buffer_id);
}

}  // namespace
}  // namespace camera
