// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>

#include <limits>
#include <memory>
#include <utility>

#include <soc/aml-common/aml-tdm-audio.h>

void AmlTdmDevice::InitMclk() {
  zx_off_t mclk_a = {};
  switch (version_) {
    case metadata::AmlVersion::kS905D2G:
      mclk_a = EE_AUDIO_MCLK_A_CTRL;
      break;
    case metadata::AmlVersion::kS905D3G:
      mclk_a = EE_AUDIO_MCLK_A_CTRL_D3G;
      break;
  }
  // Set chosen mclk channels input to selected source
  // Since this is init, set the divider to max value assuming it will
  //    be set to proper value later (slower is safer from circuit standpoint)
  // Leave disabled for now.
  zx_off_t ptr = mclk_a + (mclk_ch_ * sizeof(uint32_t));
  GetMmio().Write32((clk_src_ << 24) | 0xffff, ptr);
}

/* Notes
    -div is desired divider minus 1. (want /100? write 99)
*/
zx_status_t AmlTdmDevice::SetMclkDiv(uint32_t div) {
  // check that divider is in range
  ZX_DEBUG_ASSERT(div < (1 << kMclkDivBits));

  zx_off_t mclk_a = {};
  switch (version_) {
    case metadata::AmlVersion::kS905D2G:
      mclk_a = EE_AUDIO_MCLK_A_CTRL;
      break;
    case metadata::AmlVersion::kS905D3G:
      mclk_a = EE_AUDIO_MCLK_A_CTRL_D3G;
      break;
  }
  zx_off_t ptr = mclk_a + (mclk_ch_ * sizeof(uint32_t));
  // disable and clear out old divider value
  GetMmio().ClearBits32((1 << 31) | ((1 << kMclkDivBits) - 1), ptr);

  GetMmio().SetBits32((1 << 31) | (clk_src_ << 24) | (div & ((1 << kMclkDivBits) - 1)), ptr);
  return ZX_OK;
}

/* Notes:
    -sdiv is desired divider -1 (Want a divider of 10? write a value of 9)
*/
zx_status_t AmlTdmDevice::SetSclkDiv(uint32_t sdiv, uint32_t lrduty, uint32_t lrdiv,
                                     bool sclk_invert_ph0) {
  if (sdiv == 0) {
    // sclk needs to be at least 2x mclk.  writing a value of 0 (/1) to sdiv
    // will result in no sclk being generated on the sclk pin.  However, it
    // appears that it is running properly as a lrclk is still generated at
    // an expected rate (lrclk is derived from sclk)
    return ZX_ERR_INVALID_ARGS;
  }
  ZX_DEBUG_ASSERT(sdiv < (1 << kSclkDivBits));
  ZX_DEBUG_ASSERT(lrdiv < (1 << kLRclkDivBits));
  // lrduty is in sclk cycles, so must be less than lrdiv
  ZX_DEBUG_ASSERT(lrduty < lrdiv);

  zx_off_t ptr = EE_AUDIO_MST_A_SCLK_CTRL0 + (2 * mclk_ch_ * sizeof(uint32_t));
  GetMmio().Write32((0x3 << 30) |         // Enable the channel
                        (sdiv << 20) |    // sclk divider sclk=mclk/sdiv
                        (lrduty << 10) |  // lrclk duty cycle in sclk cycles
                        (lrdiv << 0),     // lrclk = sclk/lrdiv
                    ptr);
  GetMmio().Write32(0, ptr + sizeof(uint32_t));  // Clear delay lines for phases
  // Invert sclk.
  GetMmio().Write32(sclk_invert_ph0 << 0,
                    EE_AUDIO_MST_A_SCLK_CTRL1 + (2 * mclk_ch_ * sizeof(uint32_t)));
  return ZX_OK;
}

zx_status_t AmlTdmDevice::SetMClkPad(aml_tdm_mclk_pad_t mclk_pad) {
  switch (mclk_pad) {
    case MCLK_PAD_0:
      switch (version_) {
        case metadata::AmlVersion::kS905D2G:
          GetMmio().ModifyBits<uint32_t>(mclk_ch_, 0, 2, EE_AUDIO_MST_PAD_CTRL0);
          break;
        case metadata::AmlVersion::kS905D3G:
          GetMmio().ModifyBits<uint32_t>(mclk_ch_, 8, 2, EE_AUDIO_MST_PAD_CTRL0);
          GetMmio().ModifyBits<uint32_t>(1, 15, 1, EE_AUDIO_MST_PAD_CTRL0);  // Bit 15 to enable.
          break;
      }
      break;
    case MCLK_PAD_1:
      switch (version_) {
        case metadata::AmlVersion::kS905D2G:
          GetMmio().ModifyBits<uint32_t>(mclk_ch_, 4, 2, EE_AUDIO_MST_PAD_CTRL0);
          break;
        case metadata::AmlVersion::kS905D3G:
          GetMmio().ModifyBits<uint32_t>(mclk_ch_, 24, 2, EE_AUDIO_MST_PAD_CTRL0);
          GetMmio().ModifyBits<uint32_t>(1, 31, 1, EE_AUDIO_MST_PAD_CTRL0);  // Bit 31 to enable.
          break;
      }
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

void AmlTdmDevice::AudioClkEna(uint32_t audio_blk_mask) {
  GetMmio().SetBits32(audio_blk_mask, EE_AUDIO_CLK_GATE_EN);
}

void AmlTdmDevice::AudioClkDis(uint32_t audio_blk_mask) {
  GetMmio().ClearBits32(audio_blk_mask, EE_AUDIO_CLK_GATE_EN);
}
