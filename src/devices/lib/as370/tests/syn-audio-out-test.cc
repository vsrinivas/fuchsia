// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/shareddma/cpp/banjo-mock.h>

#include <fbl/array.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <soc/as370/as370-dma.h>
#include <soc/as370/as370-hw.h>
#include <soc/as370/syn-audio-out.h>
#include <zxtest/zxtest.h>

bool operator==(const shared_dma_protocol_t& a, const shared_dma_protocol_t& b) { return true; }
bool operator==(const dma_notify_callback_t& a, const dma_notify_callback_t& b) { return true; }

namespace audio {

class SynAudioOutTest : public zxtest::Test {
 public:
  // in 32 bits chunks.
  static constexpr size_t kGlobalRegCount = as370::kAudioGlobalSize / sizeof(uint32_t);
  static constexpr size_t kI2sRegCount = as370::kAudioI2sSize / sizeof(uint32_t);

  void SetUp() override {
    global_mocks_ = fbl::Array(new ddk_mock::MockMmioReg[kGlobalRegCount], kGlobalRegCount);
    i2s_mocks_ = fbl::Array(new ddk_mock::MockMmioReg[kI2sRegCount], kI2sRegCount);
    global_region_.emplace(global_mocks_.data(), sizeof(uint32_t), kGlobalRegCount);
    i2s_region_.emplace(i2s_mocks_.data(), sizeof(uint32_t), kI2sRegCount);
    ddk::MmioBuffer global_buffer(global_region_->GetMmioBuffer());
    ddk::MmioBuffer i2s_buffer(i2s_region_->GetMmioBuffer());

    // AIO_PRI_TSD0_PRI_CTRL disable.
    i2s()[0x000c].ExpectRead(0xffff'ffff).ExpectWrite(0xffff'fffe);
    // AIO_IRQENABLE PRI IRQ.
    i2s()[0x0150].ExpectRead(0x0000'0000).ExpectWrite(0x0000'0001);
    // AIO_PRI_PRIPORT enable.
    i2s()[0x0024].ExpectRead(0x0000'0000).ExpectWrite(0x0000'0001);

    device_ = SynAudioOutDevice::Create(std::move(global_buffer), std::move(i2s_buffer),
                                        dma_mock_.GetProto());
  }

  void TearDown() override {
    global_region_->VerifyAll();
    i2s_region_->VerifyAll();
  }

  SynAudioOutDevice& device() { return *device_.get(); }
  // Note that Mock MMIO register regionsa [] operator is via offset not index.
  ddk_mock::MockMmioRegRegion& global() { return global_region_.value(); }
  ddk_mock::MockMmioRegRegion& i2s() { return i2s_region_.value(); }
  ddk::MockSharedDma& dma() { return dma_mock_; }

 private:
  std::unique_ptr<SynAudioOutDevice> device_;
  ddk::MockSharedDma dma_mock_;
  fbl::Array<ddk_mock::MockMmioReg> global_mocks_;
  fbl::Array<ddk_mock::MockMmioReg> i2s_mocks_;
  std::optional<ddk_mock::MockMmioRegRegion> global_region_;
  std::optional<ddk_mock::MockMmioRegRegion> i2s_region_;
};

TEST_F(SynAudioOutTest, Start) {
  dma().ExpectStart(DmaId::kDmaIdMa0);

  // AIO_PRI_TSD0_PRI_CTRL enable but muted.
  i2s()[0x000c].ExpectWrite(0x0000'0003);

  // AIO_MCLKPRI_ACLK_CTRL MCLK /8 (clkSel = 4).
  i2s()[0x0164].ExpectWrite(0x0000'0189);

  // AIO_PRI_PRIAUD_CLKDIV BCLK /8 (SETTING = 3).
  i2s()[0x0000].ExpectWrite(0x0000'0003);

  // AIO_PRI_PRIAUD_CTRL I2S 32/32 bits.
  i2s()[0x0004].ExpectWrite(0x0000'0942);

  // AIO_PRI_TSD0_PRI_CTRL enable and unmute.
  i2s()[0x000c].ExpectWrite(0x0000'0001);

  EXPECT_EQ(device().Start(), 0);
}

TEST_F(SynAudioOutTest, Stop) {
  // AIO_PRI_TSD0_PRI_CTRL disable and mute.
  i2s()[0x000c].ExpectRead(0xffff'fffd).ExpectWrite(0xffff'ffff);

  dma().ExpectStop(DmaId::kDmaIdMa0);

  device().Stop();
}

TEST_F(SynAudioOutTest, Shutdown) {
  // AIO_PRI_TSD0_PRI_CTRL mute.
  i2s()[0x000c].ExpectRead(0xffff'fffd).ExpectWrite(0xffff'ffff);

  dma().ExpectStop(DmaId::kDmaIdMa0);

  // AIO_PRI_PRIPORT disable.
  i2s()[0x0024].ExpectRead(0xffff'ffff).ExpectWrite(0xffff'fffe);

  device().Shutdown();
}

}  // namespace audio
