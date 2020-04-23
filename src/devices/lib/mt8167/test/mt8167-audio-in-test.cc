// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mmio/mmio.h>

#include <fbl/auto_call.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <soc/mt8167/mt8167-audio-in.h>
#include <soc/mt8167/mt8167-hw.h>

namespace {
constexpr size_t kAudioRegSize = MT8167_AUDIO_SIZE / sizeof(uint32_t);
constexpr size_t kClockRegSize = MT8167_XO_SIZE / sizeof(uint32_t);
constexpr size_t kPllRegSize = MT8167_PLL_SIZE / sizeof(uint32_t);
class MtAudioInDeviceTest : public MtAudioInDevice {
 public:
  static std::unique_ptr<MtAudioInDeviceTest> Create(ddk_mock::MockMmioRegRegion* audio_region,
                                                     ddk_mock::MockMmioRegRegion* clock_region,
                                                     ddk_mock::MockMmioRegRegion* pll_region) {
    // Backup MMIOs not checked on test passing null MMIO regions.
    static fbl::Array<ddk_mock::MockMmioReg> backup_audio_mocks =
        fbl::Array(new ddk_mock::MockMmioReg[kAudioRegSize], kAudioRegSize);
    static fbl::Array<ddk_mock::MockMmioReg> backup_clock_mocks =
        fbl::Array(new ddk_mock::MockMmioReg[kClockRegSize], kClockRegSize);
    static fbl::Array<ddk_mock::MockMmioReg> backup_pll_mocks =
        fbl::Array(new ddk_mock::MockMmioReg[kPllRegSize], kPllRegSize);
    static ddk_mock::MockMmioRegRegion backup_audio_region(backup_audio_mocks.data(),
                                                           sizeof(uint32_t), kAudioRegSize);
    static ddk_mock::MockMmioRegRegion backup_clock_region(backup_clock_mocks.data(),
                                                           sizeof(uint32_t), kClockRegSize);
    static ddk_mock::MockMmioRegRegion backup_pll_region(backup_pll_mocks.data(), sizeof(uint32_t),
                                                         kPllRegSize);
    ddk::MmioBuffer b1(audio_region ? audio_region->GetMmioBuffer()
                                    : backup_audio_region.GetMmioBuffer());
    ddk::MmioBuffer b2(clock_region ? clock_region->GetMmioBuffer()
                                    : backup_clock_region.GetMmioBuffer());
    ddk::MmioBuffer b3(pll_region ? pll_region->GetMmioBuffer()
                                  : backup_pll_region.GetMmioBuffer());
    fbl::AllocChecker ac;
    auto dev = std::unique_ptr<MtAudioInDeviceTest>(
        new (&ac) MtAudioInDeviceTest(std::move(b1), std::move(b2), std::move(b3), 0));
    if (!ac.check()) {
      return nullptr;
    }
    return dev;
  }
  MtAudioInDeviceTest(ddk::MmioBuffer mmio_audio, ddk::MmioBuffer mmio_clk,
                      ddk::MmioBuffer mmio_pll, uint32_t fifo_depth)
      : MtAudioInDevice(std::move(mmio_audio), std::move(mmio_clk), std::move(mmio_pll),
                        fifo_depth) {}
  void InitializeRegisters() { MtAudioInDevice::InitRegs(); }
};

}  // namespace

namespace audio {
namespace mt8167 {

TEST(Mt8167AudioInTest, SetRate44100Based) {
  fbl::Array<ddk_mock::MockMmioReg> clock_mocks =
      fbl::Array(new ddk_mock::MockMmioReg[kClockRegSize], kClockRegSize);
  fbl::Array<ddk_mock::MockMmioReg> pll_mocks =
      fbl::Array(new ddk_mock::MockMmioReg[kPllRegSize], kPllRegSize);

  ddk_mock::MockMmioRegRegion clock_region(clock_mocks.data(), sizeof(uint32_t), kClockRegSize);
  ddk_mock::MockMmioRegRegion pll_region(pll_mocks.data(), sizeof(uint32_t), kPllRegSize);

  pll_mocks[0x180 / 4].ExpectRead(0x0000'0000).ExpectWrite(0x0000'0001);    // Enable Aud1.
  clock_mocks[0x044 / 4].ExpectRead(0xffff'ffff).ExpectWrite(0xfffd'ffff);  // Use Aud1.

  // Set clock for 11025Hz (44100/4).  For 44100 based clocks we use the clock source
  // Aud1 = 180.6336MHz.  We want master clock = 22579200Mhz (44100 x 512)
  // Master clock = clk_source(Aud1) x (1 / (n + 1)), hence n = 180633600/22579200-1 = 7

  // For I2S at 16 bits we have 32 bits per frame so for 11025Hz Bit clock is at 352800Hz.
  // Bit clock = clk_source(Mck) x (1 / (n + 1)), hence n = 22579200/352800-1=63
  clock_mocks[0x04c / 4].ExpectRead(0x0000'0000).ExpectWrite(0x3f00'0000);  // Bit clock, 63.
  clock_mocks[0x04c / 4].ExpectRead(0x0000'0000).ExpectWrite(0x0007'0000);  // Master clock, 7.

  auto dev = MtAudioInDeviceTest::Create(nullptr, &clock_region, &pll_region);
  EXPECT_OK(dev->SetRate(11025));
  clock_region.VerifyAll();
}

TEST(Mt8167AudioInTest, SetRate48000Based) {
  fbl::Array<ddk_mock::MockMmioReg> clock_mocks =
      fbl::Array(new ddk_mock::MockMmioReg[kClockRegSize], kClockRegSize);
  fbl::Array<ddk_mock::MockMmioReg> pll_mocks =
      fbl::Array(new ddk_mock::MockMmioReg[kPllRegSize], kPllRegSize);

  ddk_mock::MockMmioRegRegion clock_region(clock_mocks.data(), sizeof(uint32_t), kClockRegSize);
  ddk_mock::MockMmioRegRegion pll_region(pll_mocks.data(), sizeof(uint32_t), kPllRegSize);

  pll_mocks[0x1a0 / 4].ExpectRead(0x0000'0000).ExpectWrite(0x0000'0001);    // Enable Aud2.
  clock_mocks[0x044 / 4].ExpectRead(0x0000'0000).ExpectWrite(0x0002'0000);  // Use Aud2.

  // Set clock for 192000Hz (48000x4).  For 48000 based clocks we use the clock source
  // Aud2 = 196.608MHz.  We want master clock = 24576000Hz (48000 x 512)
  // Master clock = clk_source(Aud2) x (1 / (n + 1)), hence n = 196608000/24576000-1 = 7

  // For I2S at 16 bits we have 32 bits per frame so for 192000Hz Bit clock is at 6144000Hz.
  // Bit clock = clk_source(Mck) x (1 / (n + 1)), hence n = 24576000/6144000-1=3
  clock_mocks[0x04c / 4].ExpectRead(0x0000'0000).ExpectWrite(0x0300'0000);  // Bit clock, 3.
  clock_mocks[0x04c / 4].ExpectRead(0x0000'0000).ExpectWrite(0x0007'0000);  // Master clock, 7.

  auto dev = MtAudioInDeviceTest::Create(nullptr, &clock_region, &pll_region);
  EXPECT_OK(dev->SetRate(192000));
  clock_region.VerifyAll();
}

TEST(Mt8167AudioInTest, Set32Bits) {
  fbl::Array<ddk_mock::MockMmioReg> audio_mocks =
      fbl::Array(new ddk_mock::MockMmioReg[kAudioRegSize], kAudioRegSize);
  ddk_mock::MockMmioRegRegion audio_region(audio_mocks.data(), sizeof(uint32_t), kAudioRegSize);

  audio_mocks[0x588 / 4].ExpectWrite(0x1f00'230d);  // TDM IN enabled, i2s, 32 bits, 32 BCK, 2ch.
  auto dev = MtAudioInDeviceTest::Create(&audio_region, nullptr, nullptr);
  EXPECT_OK(dev->SetBitsPerSample(32));

  audio_region.VerifyAll();
}

TEST(Mt8167AudioInTest, Set24Bits) {
  fbl::Array<ddk_mock::MockMmioReg> audio_mocks =
      fbl::Array(new ddk_mock::MockMmioReg[kAudioRegSize], kAudioRegSize);
  ddk_mock::MockMmioRegRegion audio_region(audio_mocks.data(), sizeof(uint32_t), kAudioRegSize);

  audio_mocks[0x588 / 4].ExpectWrite(0x1700'120d);  // TDM IN enabled, i2s, 24 bits, 24 BCK, 2ch.
  auto dev = MtAudioInDeviceTest::Create(&audio_region, nullptr, nullptr);
  EXPECT_OK(dev->SetBitsPerSample(24));

  audio_region.VerifyAll();
}

TEST(Mt8167AudioInTest, Set16Bits) {
  fbl::Array<ddk_mock::MockMmioReg> audio_mocks =
      fbl::Array(new ddk_mock::MockMmioReg[kAudioRegSize], kAudioRegSize);
  ddk_mock::MockMmioRegRegion audio_region(audio_mocks.data(), sizeof(uint32_t), kAudioRegSize);

  audio_mocks[0x588 / 4].ExpectWrite(0x0f00'010d);  // TDM IN enabled, i2s, 16 bits, 16 BCK, 2ch.
  auto dev = MtAudioInDeviceTest::Create(&audio_region, nullptr, nullptr);
  EXPECT_OK(dev->SetBitsPerSample(16));

  audio_region.VerifyAll();
}

TEST(Mt8167AudioInTest, InitializeRegisters) {
  fbl::Array<ddk_mock::MockMmioReg> audio_mocks =
      fbl::Array(new ddk_mock::MockMmioReg[kAudioRegSize], kAudioRegSize);
  ddk_mock::MockMmioRegRegion audio_region(audio_mocks.data(), sizeof(uint32_t), kAudioRegSize);

  audio_mocks[0x010 / 4].ExpectRead(0x0000'0000).ExpectWrite(0x0000'0001);  // Set AFE on.
  audio_mocks[0x000 / 4]
      .ExpectRead(0xffff'ffff)
      .ExpectWrite(0xffff'fffb);                    // Set AFE power down off.
  audio_mocks[0x39c / 4].ExpectWrite(0x0000'0008);  // 040_cfg to 0 and 031_cf to 1.
  audio_mocks[0x588 / 4].ExpectWrite(0x0f00'010d);  // TDM IN enabled, i2s, 16 bits, 16 BCK, 2ch.

  auto dev = MtAudioInDeviceTest::Create(&audio_region, nullptr, nullptr);
  dev->InitializeRegisters();
  audio_region.VerifyAll();
}

}  // namespace mt8167
}  // namespace audio
