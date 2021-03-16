// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>

#include <limits>
#include <utility>

#include <fbl/alloc_checker.h>
#include <soc/mt8167/mt8167-audio-in.h>
#include <soc/mt8167/mt8167-audio-regs.h>
#include <soc/mt8167/mt8167-clk-regs.h>

std::unique_ptr<MtAudioInDevice> MtAudioInDevice::Create(ddk::MmioBuffer mmio_audio,
                                                         ddk::MmioBuffer mmio_clk,
                                                         ddk::MmioBuffer mmio_pll, MtI2sCh ch) {
  uint32_t fifo_depth = 0;  // in bytes. TODO(andresoportus): Find out actual size.

  // TODO(andresoportus): Support other configurations.
  if (ch != I2S6) {
    return nullptr;
  }

  fbl::AllocChecker ac;
  auto dev = std::unique_ptr<MtAudioInDevice>(new (&ac) MtAudioInDevice(
      std::move(mmio_audio), std::move(mmio_clk), std::move(mmio_pll), fifo_depth));
  if (!ac.check()) {
    return nullptr;
  }

  dev->InitRegs();
  return dev;
}

void MtAudioInDevice::InitRegs() {
  // Enable the AFE module.
  AFE_DAC_CON0::Get().ReadFrom(&mmio_audio_).set_AFE_ON(1).WriteTo(&mmio_audio_);

  // Power up the AFE module by clearing the power down bit.
  AUDIO_TOP_CON0::Get().ReadFrom(&mmio_audio_).set_PDN_AFE(0).WriteTo(&mmio_audio_);

  // Route TDM_IN to afe_mem_if.
  AFE_CONN_TDMIN_CON::Get().FromValue(0).set_o_40_cfg(0).set_o_41_cfg(1).WriteTo(&mmio_audio_);

  // Audio Interface.
  SetBitsPerSample(16);
}

zx_status_t MtAudioInDevice::SetBitsPerSample(uint32_t bits_per_sample) {
  if (bits_per_sample != 16 && bits_per_sample != 32 && bits_per_sample != 24) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  bits_per_sample_ = bits_per_sample;
  auto con1 = AFE_TDM_IN_CON1::Get()
                  .FromValue(0)
                  .set_tdm_en(1)
                  .set_tdm_fmt(1)
                  .  // I2S.
              set_tdm_lrck_inv(1)
                  .set_tdm_channel(0);  // 2 ch.
  if (bits_per_sample_ == 16) {
    con1.set_tdm_wlen(1).set_LRCK_TDM_WIDTH(15).set_fast_lrck_cycle_sel(0);  // LRCK.
  } else if (bits_per_sample_ == 24) {
    con1.set_tdm_wlen(2).set_LRCK_TDM_WIDTH(23).set_fast_lrck_cycle_sel(1);  // LRCK.
  } else {  // bits_per_sample_ == 32
    con1.set_tdm_wlen(3).set_LRCK_TDM_WIDTH(31).set_fast_lrck_cycle_sel(2);  // LRCK.
  }
  con1.WriteTo(&mmio_audio_);
  SetRate(frames_per_second_);
  return ZX_OK;
}

zx_status_t MtAudioInDevice::SetRate(uint32_t frames_per_second) {
  // BCK = Aud1-Aud2 PLL / 8 / n = frames_per_second * 32.
  uint32_t n = 0;
  switch (frames_per_second) {
    case 11025:   // n = 16 * 44100 / 11025  = 64, BCK = 352.8 kHz.
    case 22050:   // n = 16 * 44100 / 22050  = 32, BCK = 705.6 KHz.
    case 44100:   // n = 16 * 44100 / 44100  = 16, BCK = 1.4112 MHz.
    case 88200:   // n = 16 * 44100 / 88200  =  8, BCK = 2.8224 MHz.
    case 176400:  // n = 16 * 44100 / 176400 =  4, BCK = 5.6448 MHz.
      n = 16 * 44100 / frames_per_second;
      break;
    case 8000:    // n = 16 * 48000 / 8000   = 96, BCK = 256 KHz.
    case 12000:   // n = 16 * 48000 / 12000  = 64, BCK = 384 KHz.
    case 16000:   // n = 16 * 48000 / 16000  = 48, BCK = 512 KHz.
    case 24000:   // n = 16 * 48000 / 24000  = 32, BCK = 768 KHz.
    case 32000:   // n = 16 * 48000 / 32000  = 24, BCK = 1.024 MHz.
    case 48000:   // n = 16 * 48000 / 48000  = 16, BCK = 1.536 MHz.
    case 96000:   // n = 16 * 48000 / 96000  =  8, BCK = 3.072 MHz.
    case 192000:  // n = 16 * 48000 / 192000 =  4, BCK = 6.144 MHz.
      n = 16 * 48000 / frames_per_second;
      break;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
  CLK_SEL_11::Get().ReadFrom(&mmio_clk_).set_apll12_ck_div5b(n - 1).WriteTo(&mmio_clk_);  // BCK.

  frames_per_second_ = frames_per_second;
  if (frames_per_second % 8000) {
    // Use aud1 clock.  Enable aud1 PLL.
    APLL1_CON0::Get().ReadFrom(&mmio_pll_).set_APLL1_EN(1).WriteTo(&mmio_pll_);
    // MCLK of I2S6 (TDM IN, i2s5 in 0-5 range) to hf_faud_1_ck (aud1).
    CLK_SEL_9::Get().ReadFrom(&mmio_clk_).set_apll_i2s5_mck_sel(0).WriteTo(&mmio_clk_);
    // MCK = 180.6336 MHz(Aud1 PLL) / 7+1 = 22.5792 MHz.
    CLK_SEL_11::Get().ReadFrom(&mmio_clk_).set_apll12_ck_div5(7).WriteTo(&mmio_clk_);
  } else {
    // Use aud2 clock.  Enable aud2 PLL.
    APLL2_CON0::Get().ReadFrom(&mmio_pll_).set_APLL2_EN(1).WriteTo(&mmio_pll_);
    // MCLK of I2S6 (TDM IN, i2s5 in 0-5 range) to hf_faud_2_ck (aud2).
    CLK_SEL_9::Get().ReadFrom(&mmio_clk_).set_apll_i2s5_mck_sel(1).WriteTo(&mmio_clk_);
    // MCK = 196.608 MHz(Aud2 PLL) / 7+1 = 24.576 MHz.
    CLK_SEL_11::Get().ReadFrom(&mmio_clk_).set_apll12_ck_div5(7).WriteTo(&mmio_clk_);
  }
  return ZX_OK;
}

uint32_t MtAudioInDevice::GetRingPosition() {
  return AFE_HDMI_IN_2CH_CUR::Get().ReadFrom(&mmio_audio_).reg_value() -
         AFE_HDMI_IN_2CH_BASE::Get().ReadFrom(&mmio_audio_).reg_value();
}

zx_status_t MtAudioInDevice::SetBuffer(zx_paddr_t buf, size_t len) {
  if ((buf % 16) || ((buf + len - 1) > std::numeric_limits<uint32_t>::max()) || (len < 16) ||
      (len % 16)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // End is inclusive.
  AFE_HDMI_IN_2CH_BASE::Get().FromValue(static_cast<uint32_t>(buf)).WriteTo(&mmio_audio_);
  auto end = AFE_HDMI_IN_2CH_END::Get().FromValue(static_cast<uint32_t>(buf + len - 1));
  end.WriteTo(&mmio_audio_);
  return ZX_OK;
}

uint64_t MtAudioInDevice::Start() {
  // Power up by clearing the power down (pdn) bit.
  CLK_SEL_9::Get().ReadFrom(&mmio_clk_).set_apll12_div5_pdn(0).WriteTo(&mmio_clk_);   // MCK.
  CLK_SEL_9::Get().ReadFrom(&mmio_clk_).set_apll12_div5b_pdn(0).WriteTo(&mmio_clk_);  // BCK.

  auto in = AFE_HDMI_IN_2CH_CON0::Get().ReadFrom(&mmio_audio_).set_AFE_HDMI_IN_2CH_OUT_ON(1);
  in.WriteTo(&mmio_audio_);
  return 0;
}

void MtAudioInDevice::Stop() {
  // Power down by setting the power down (pdn) bit.
  CLK_SEL_9::Get().ReadFrom(&mmio_clk_).set_apll12_div5_pdn(1).WriteTo(&mmio_clk_);   // MCK.
  CLK_SEL_9::Get().ReadFrom(&mmio_clk_).set_apll12_div5b_pdn(1).WriteTo(&mmio_clk_);  // BCK.

  auto in = AFE_HDMI_IN_2CH_CON0::Get().ReadFrom(&mmio_audio_).set_AFE_HDMI_IN_2CH_OUT_ON(0);
  in.WriteTo(&mmio_audio_);
}

void MtAudioInDevice::Shutdown() {
  Stop();
  // Disable the AFE module.
  // TODO(andresoportus): Manage multiple drivers accessing same registers, e.g. Audio In and Out.
  AFE_DAC_CON0::Get().ReadFrom(&mmio_audio_).set_AFE_ON(0).WriteTo(&mmio_audio_);
}
