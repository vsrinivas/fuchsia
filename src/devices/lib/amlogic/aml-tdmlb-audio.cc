// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>

#include <limits>
#include <memory>
#include <utility>

#include <fbl/alloc_checker.h>
#include <soc/aml-common/aml-loopback-audio.h>
#include <soc/aml-common/aml-tdm-audio.h>

// static
std::unique_ptr<AmlTdmDevice> AmlTdmLbDevice::Create(fdf::MmioBuffer mmio, ee_audio_mclk_src_t src,
                                                     aml_toddr_t toddr, aml_tdm_mclk_t mclk,
                                                     metadata::AmlLoopbackConfig loopback_config,
                                                     metadata::AmlVersion version) {
  uint32_t fifo_depth = {};  // in bytes.
  uint32_t lb_src = {};
  switch (version) {
    case metadata::AmlVersion::kS905D2G:
    case metadata::AmlVersion::kS905D3G:
      __FALLTHROUGH;
    case metadata::AmlVersion::kA5:
      fifo_depth = 64 * 8;  // TODDR_A/B has 64 x 64-bit
      lb_src = ToTdminLbSrcV2(loopback_config.datalb_src);
      break;
  }

  [[maybe_unused]] auto unused =
      audio::aml_g12::AmlLoopbackDevice::Create(mmio, version, loopback_config);

  fbl::AllocChecker ac;
  auto dev = std::unique_ptr<AmlTdmLbDevice>(
      new (&ac) AmlTdmLbDevice(std::move(mmio), src, toddr, mclk, fifo_depth, version, lb_src));
  if (!ac.check()) {
    return nullptr;
  }

  return dev;
}

void AmlTdmLbDevice::Initialize() {
  // Enable the audio domain clocks used by this instance.
  AudioClkEna((EE_AUDIO_CLK_GATE_TDMINA << tdm_ch_) | (EE_AUDIO_CLK_GATE_TODDRA << toddr_ch_) |
              EE_AUDIO_CLK_GATE_ARB | EE_AUDIO_CLK_GATE_LOOPBACK);

  InitMclk();

  // Set the sclk and lrclk sources to the chosen mclk channel
  zx_off_t ptr = EE_AUDIO_CLK_TDMIN_A_CTL + tdm_ch_ * sizeof(uint32_t);
  constexpr bool sclk_inv = true;  // Invert sclk wrt TDMOUT.
  mmio_.Write32((0x03 << 30) | (sclk_inv << 29) | (mclk_ch_ << 24) | (mclk_ch_ << 20), ptr);

  // Disable the TODDR Channel
  // Only use one buffer
  // Interrupts off
  // ack delay = 0
  // set destination tdm block and enable that selection
  switch (version_) {
    case metadata::AmlVersion::kS905D3G:
    case metadata::AmlVersion::kS905D2G:
      __FALLTHROUGH;
    case metadata::AmlVersion::kA5:
      mmio_.Write32((0x00 << 13) |   // Packed.
                        (31 << 8) |  // MSB position of data.
                        (16 << 3),   // LSB position of data. - (S/U32 - 0; S/U16 - 16)
                    GetToddrOffset(TODDR_CTRL0_OFFS));
      mmio_.Write32((0x7 << 26) |    // select |loopback_a| as data Source
                        (1 << 25) |  // set the magic force end bit(25) to cause fetch from start
                        ((fifo_depth_ / 8 / 2 - 1) << 12) |  // trigger ddr when fifo half full
                        (0x02 << 8),                         // STATUS2 source is ddr position
                    GetToddrOffset(TODDR_CTRL1_OFFS));
      break;
  }

  // Value to be inserted in a slot if it is muted
  mmio_.Write32(0x00000000, GetTdmOffset(TDMIN_MUTE_VAL_OFFS));

  mmio_.Write32(0x00000000, GetTdmOffset(TDMIN_MUTE0_OFFS));  // Disable lane 0 muting.
  mmio_.Write32(0x00000000, GetTdmOffset(TDMIN_MUTE1_OFFS));  // Disable lane 1 muting.
  mmio_.Write32(0x00000000, GetTdmOffset(TDMIN_MUTE2_OFFS));  // Disable lane 2 muting.
  mmio_.Write32(0x00000000, GetTdmOffset(TDMIN_MUTE3_OFFS));  // Disable lane 3 muting.
}

uint32_t AmlTdmLbDevice::GetRingPosition() {
  return mmio_.Read32(GetToddrOffset(TODDR_STATUS2_OFFS)) -
         mmio_.Read32(GetToddrOffset(TODDR_START_ADDR_OFFS));
}

uint32_t AmlTdmLbDevice::GetDmaStatus() { return mmio_.Read32(GetToddrOffset(TODDR_STATUS1_OFFS)); }

uint32_t AmlTdmLbDevice::GetTdmStatus() { return mmio_.Read32(GetTdmOffset(TDMIN_CTRL_OFFS)); }

zx_status_t AmlTdmLbDevice::SetBuffer(zx_paddr_t buf, size_t len) {
  // Ensure ring buffer resides in lower memory (dma pointers are 32-bit)
  //    and len is at least 8 (size of each dma operation)
  if (((buf + len - 1) > std::numeric_limits<uint32_t>::max()) || (len < 8)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Write32 the start and end pointers.  Each fetch is 64-bits, so end pointer
  //    is pointer to the last 64-bit fetch (inclusive)
  mmio_.Write32(static_cast<uint32_t>(buf), GetToddrOffset(TODDR_START_ADDR_OFFS));
  mmio_.Write32(static_cast<uint32_t>(buf), GetToddrOffset(TODDR_INIT_ADDR_OFFS));
  mmio_.Write32(static_cast<uint32_t>(buf + len - 8), GetToddrOffset(TODDR_FINISH_ADDR_OFFS));
  return ZX_OK;
}

// bit_offset - bit position in frame where first slot will appear.
// num_slots - number of slots per frame minus one.
// bits_per_slot - width of each slot minus one.
// bits_per_sample - number of bits in sample minus one.
// mix_mask - lanes to mix L+R.
void AmlTdmLbDevice::ConfigTdmSlot(uint8_t bit_offset, uint8_t num_slots, uint8_t bits_per_slot,
                                   uint8_t bits_per_sample, uint8_t mix_mask, bool i2s_mode) {
  switch (version_) {
    case metadata::AmlVersion::kS905D3G:
    case metadata::AmlVersion::kS905D2G: {
      __FALLTHROUGH;
      case metadata::AmlVersion::kA5: {
        uint32_t reg0 = (i2s_mode << 30) |    // TDM/I2S mode.
                        (lb_src_ << 20) |     // select source for |TDMIN_LB|.
                        (bit_offset << 16) |  // Add delay to ws or data for skew modification.
                        bits_per_slot;
        mmio_.Write32(reg0, GetTdmOffset(TDMIN_CTRL_OFFS));
      } break;
    }
  }
}

zx_status_t AmlTdmLbDevice::ConfigTdmLane(size_t lane, uint32_t enable_mask, uint32_t mute_mask) {
  switch (lane) {
    case 0:
      mmio_.Write32(enable_mask, GetTdmOffset(TDMIN_MASK0_OFFS));
      mmio_.Write32(mute_mask, GetTdmOffset(TDMIN_MUTE0_OFFS));
      break;
    case 1:
      mmio_.Write32(enable_mask, GetTdmOffset(TDMIN_MASK1_OFFS));
      mmio_.Write32(mute_mask, GetTdmOffset(TDMIN_MUTE1_OFFS));
      break;
    case 2:
      mmio_.Write32(enable_mask, GetTdmOffset(TDMIN_MASK2_OFFS));
      mmio_.Write32(mute_mask, GetTdmOffset(TDMIN_MUTE2_OFFS));
      break;
    case 3:
      mmio_.Write32(enable_mask, GetTdmOffset(TDMIN_MASK3_OFFS));
      mmio_.Write32(mute_mask, GetTdmOffset(TDMIN_MUTE3_OFFS));
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

void AmlTdmLbDevice::ConfigTdmSwaps(uint32_t swaps) {
  mmio_.Write32(swaps, GetTdmOffset(TDMIN_SWAP_OFFS));
}

// Stops the loopback/tdm from clocking data out of fifo onto bus
void AmlTdmLbDevice::TdmInDisable() {
  mmio_.ClearBits32(1 << 31, EE_AUDIO_LB_A_CTRL0);
  mmio_.ClearBits32(1 << 31, GetTdmOffset(TDMIN_CTRL_OFFS));
}
// Enables the loopback/tdm to clock data out of fifo onto bus
void AmlTdmLbDevice::TdmInEnable() {
  mmio_.SetBits32(1 << 31, EE_AUDIO_LB_A_CTRL0);
  mmio_.SetBits32(1 << 31, GetTdmOffset(TDMIN_CTRL_OFFS));
}

void AmlTdmLbDevice::TODDREnable() { mmio_.SetBits32(1 << 31, GetToddrOffset(TODDR_CTRL0_OFFS)); }

void AmlTdmLbDevice::TODDRDisable() {
  // Disable the toddr channel
  mmio_.ClearBits32(1 << 31, GetToddrOffset(TODDR_CTRL0_OFFS));
}

void AmlTdmLbDevice::Sync() {
  mmio_.ClearBits32(3 << 28, GetTdmOffset(TDMIN_CTRL_OFFS));
  mmio_.SetBits32(1 << 29, GetTdmOffset(TDMIN_CTRL_OFFS));
  mmio_.SetBits32(1 << 28, GetTdmOffset(TDMIN_CTRL_OFFS));
}

// Resets toddr mechanisms to start at beginning of buffer
//   starts the toddr (this will fill the fifo)
//   starts the tdm to clock out data on the bus
// returns the start time
uint64_t AmlTdmLbDevice::Start() {
  uint64_t a, b;

  Sync();
  TODDREnable();
  a = zx_clock_get_monotonic();
  TdmInEnable();
  b = zx_clock_get_monotonic();
  return ((b - a) >> 1) + a;
}

void AmlTdmLbDevice::Stop() {
  TdmInDisable();
  TODDRDisable();
}

void AmlTdmLbDevice::Shutdown() {
  Stop();

  // Disable the output signals
  zx_off_t ptr = EE_AUDIO_CLK_TDMIN_A_CTL + tdm_ch_ * sizeof(uint32_t);
  mmio_.ClearBits32(0x03 << 30, ptr);

  // Disable the audio domain clocks used by this instance.
  AudioClkDis((EE_AUDIO_CLK_GATE_TDMINA << tdm_ch_) | (EE_AUDIO_CLK_GATE_TODDRA << toddr_ch_) |
              EE_AUDIO_CLK_GATE_LOOPBACK);

  // Note: We are leaving the ARB unit clocked as well as MCLK and
  //  SCLK generation units since it is possible they are used by
  //  some other audio driver outside of this instance
}
