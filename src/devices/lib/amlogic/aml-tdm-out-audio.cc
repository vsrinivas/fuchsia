// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>

#include <limits>
#include <memory>
#include <utility>

#include <fbl/alloc_checker.h>
#include <soc/aml-common/aml-tdm-audio.h>

// static
std::unique_ptr<AmlTdmDevice> AmlTdmOutDevice::Create(fdf::MmioBuffer mmio, ee_audio_mclk_src_t src,
                                                      aml_tdm_out_t tdm, aml_frddr_t frddr,
                                                      aml_tdm_mclk_t mclk,
                                                      metadata::AmlVersion version) {
  // FRDDR A has 256 64-bit lines in the FIFO, B and C have 128.
  uint32_t fifo_depth = 128 * 8;  // in bytes.
  if (frddr == FRDDR_A) {
    fifo_depth = 256 * 8;
  }

  fbl::AllocChecker ac;
  auto dev = std::unique_ptr<AmlTdmDevice>(
      new (&ac) AmlTdmOutDevice(std::move(mmio), src, tdm, frddr, mclk, fifo_depth, version));
  if (!ac.check()) {
    return nullptr;
  }

  return dev;
}

void AmlTdmOutDevice::Initialize() {
  // Enable the audio domain clocks used by this instance.
  AudioClkEna((EE_AUDIO_CLK_GATE_TDMOUTA << tdm_ch_) | (EE_AUDIO_CLK_GATE_FRDDRA << frddr_ch_) |
              EE_AUDIO_CLK_GATE_ARB);

  InitMclk();

  // Set the sclk and lrclk sources to the chosen mclk channel
  zx_off_t ptr = EE_AUDIO_CLK_TDMOUT_A_CTL + tdm_ch_ * sizeof(uint32_t);

  // We set the Frame Sync sclk invert bit that shifts the delta between FS and DATA, and
  // allows FS of width 1.
  constexpr uint32_t sclk_ws_inv = 1;
  mmio_.Write32((0x03 << 30) | (sclk_ws_inv << 28) | (mclk_ch_ << 24) | (mclk_ch_ << 20), ptr);

  // Enable DDR ARB, and enable this ddr channels bit.
  mmio_.SetBits32((1 << 31) | (1 << (4 + frddr_ch_)), EE_AUDIO_ARB_CTRL);

  // Disable the FRDDR Channel
  // Only use one buffer
  // Interrupts on for FIFO errors
  // ack delay = 0
  // set destination tdm block and enable that selection
  switch (version_) {
    case metadata::AmlVersion::kS905D2G:
      mmio_.Write32(tdm_ch_ | (0x30 << 16) | (1 << 3), GetFrddrOffset(FRDDR_CTRL0_OFFS));
      break;
    case metadata::AmlVersion::kS905D3G:
      mmio_.Write32(tdm_ch_ | (1 << 4), GetFrddrOffset(FRDDR_CTRL2_OFFS_D3G));
      break;
  }
  // use entire fifo, start transfer request when fifo is at 1/2 full
  // set the magic force end bit(12) to cause fetch from start
  //    -this only happens when the bit is set from 0->1 (edge)
  // fifo depth needs to be configured in terms of 64-bit lines.
  mmio_.Write32((1 << 12) | (((fifo_depth_ / 8) - 1) << 24) | ((((fifo_depth_ / 8) / 2) - 1) << 16),
                GetFrddrOffset(FRDDR_CTRL1_OFFS));

  // Value to be inserted in a slot if it is muted
  mmio_.Write32(0x00000000, GetTdmOffset(TDMOUT_MUTE_VAL_OFFS));
  // Value to be inserted in a slot if it is masked
  mmio_.Write32(0x00000000, GetTdmOffset(TDMOUT_MASK_VAL_OFFS));

  mmio_.Write32(0x00000000, GetTdmOffset(TDMOUT_MUTE0_OFFS));  // Disable lane 0 muting.
  mmio_.Write32(0x00000000, GetTdmOffset(TDMOUT_MUTE1_OFFS));  // Disable lane 1 muting.
  mmio_.Write32(0x00000000, GetTdmOffset(TDMOUT_MUTE2_OFFS));  // Disable lane 2 muting.
  mmio_.Write32(0x00000000, GetTdmOffset(TDMOUT_MUTE3_OFFS));  // Disable lane 3 muting.

  // Datasheets state that PAD_CTRL1 controls sclk and lrclk source selection (which mclk),
  // it does this per pad (0, 1, 2).  These pads are tied to the TDM channel in use
  // (this is not specified in the datasheets but confirmed empirically) such that TDM_OUT_A
  // corresponds to pad 0, TDM_OUT_B to pad 1, and TDM_OUT_C to pad 2.
  uint32_t pad1 = {};
  switch (version_) {
    case metadata::AmlVersion::kS905D2G:
      pad1 = EE_AUDIO_MST_PAD_CTRL1;
      break;
    case metadata::AmlVersion::kS905D3G:
      pad1 = EE_AUDIO_MST_PAD_CTRL1_D3G;
      break;
  }
  // Only modify the part of the MST PAD register that corresponds to the engine in use.
  switch (tdm_ch_) {
    case TDM_OUT_A:
      mmio_.ModifyBits32((mclk_ch_ << 16) | (mclk_ch_ << 0), (7 << 16) | (7 << 0), pad1);
      break;
    case TDM_OUT_B:
      mmio_.ModifyBits32((mclk_ch_ << 20) | (mclk_ch_ << 4), (7 << 20) | (7 << 4), pad1);
      break;
    case TDM_OUT_C:
      mmio_.ModifyBits32((mclk_ch_ << 24) | (mclk_ch_ << 8), (7 << 24) | (7 << 8), pad1);
      break;
  }
}

uint32_t AmlTdmOutDevice::GetRingPosition() {
  return mmio_.Read32(GetFrddrOffset(FRDDR_STATUS2_OFFS)) -
         mmio_.Read32(GetFrddrOffset(FRDDR_START_ADDR_OFFS));
}

uint32_t AmlTdmOutDevice::GetDmaStatus() {
  return mmio_.Read32(GetFrddrOffset(FRDDR_STATUS1_OFFS));
}

uint32_t AmlTdmOutDevice::GetTdmStatus() { return mmio_.Read32(GetTdmOffset(TDMOUT_CTRL0_OFFS)); }

zx_status_t AmlTdmOutDevice::SetBuffer(zx_paddr_t buf, size_t len) {
  // Ensure ring buffer resides in lower memory (dma pointers are 32-bit)
  //    and len is at least 8 (size of each dma operation)
  if (((buf + len - 1) > std::numeric_limits<uint32_t>::max()) || (len < 8)) {
    return ZX_ERR_INVALID_ARGS;
  }
  mmio_.Write32(buf, GetFrddrOffset(FRDDR_INT_ADDR_OFFS));

  // Write32 the start and end pointers.  Each fetch is 64-bits, so end pointer
  //    is pointer to the last 64-bit fetch (inclusive)
  mmio_.Write32(static_cast<uint32_t>(buf), GetFrddrOffset(FRDDR_START_ADDR_OFFS));
  mmio_.Write32(static_cast<uint32_t>(buf + len - 8), GetFrddrOffset(FRDDR_FINISH_ADDR_OFFS));
  return ZX_OK;
}

/*
    bit_offset - bit position in frame where first slot will appear
                    (position 0 is concurrent with frame sync)
    num_slots - number of slots per frame minus one
    bits_per_slot - width of each slot minus one
    bits_per_sample - number of bits in sample minus one
    mix_mask - lanes to mix L+R.
*/
void AmlTdmOutDevice::ConfigTdmSlot(uint8_t bit_offset, uint8_t num_slots, uint8_t bits_per_slot,
                                    uint8_t bits_per_sample, uint8_t mix_mask, bool i2s_mode) {
  switch (version_) {
    case metadata::AmlVersion::kS905D2G: {
      uint32_t reg0 = bits_per_slot | (num_slots << 5) | (bit_offset << 15) | (mix_mask << 20);
      mmio_.Write32(reg0, GetTdmOffset(TDMOUT_CTRL0_OFFS));
    } break;
    case metadata::AmlVersion::kS905D3G: {
      uint32_t reg0 =
          bits_per_slot | (num_slots << 5) | (bit_offset << 15) | (1 << 31);  // Bit 31 to enable.
      mmio_.Write32(reg0, GetTdmOffset(TDMOUT_CTRL0_OFFS));
      uint32_t reg2 = (mix_mask << 0);
      mmio_.Write32(reg2, GetTdmOffset(TDMOUT_CTRL2_OFFS_D3G));
    } break;
  }

  uint32_t reg = (bits_per_sample << 8) | (frddr_ch_ << 24);
  if (bits_per_sample <= 8) {
    // 8 bit sample, left justify in frame, split 64-bit dma fetch into 8 samples
    reg |= (0 << 4);
  } else if (bits_per_sample <= 16) {
    // 16 bit sample, left justify in frame, split 64-bit dma fetch into 4 samples
    reg |= (2 << 4);
  } else {
    // 32/24 bit sample, left justify in slot, split 64-bit dma fetch into 2 samples
    reg |= (4 << 4);
  }
  mmio_.Write32(reg, GetTdmOffset(TDMOUT_CTRL1_OFFS));
}

zx_status_t AmlTdmOutDevice::ConfigTdmLane(size_t lane, uint32_t enable_mask, uint32_t mute_mask) {
  switch (lane) {
    case 0:
      mmio_.Write32(enable_mask, GetTdmOffset(TDMOUT_MASK0_OFFS));
      mmio_.Write32(mute_mask, GetTdmOffset(TDMOUT_MUTE0_OFFS));
      break;
    case 1:
      mmio_.Write32(enable_mask, GetTdmOffset(TDMOUT_MASK1_OFFS));
      mmio_.Write32(mute_mask, GetTdmOffset(TDMOUT_MUTE1_OFFS));
      break;
    case 2:
      mmio_.Write32(enable_mask, GetTdmOffset(TDMOUT_MASK2_OFFS));
      mmio_.Write32(mute_mask, GetTdmOffset(TDMOUT_MUTE2_OFFS));
      break;
    case 3:
      mmio_.Write32(enable_mask, GetTdmOffset(TDMOUT_MASK3_OFFS));
      mmio_.Write32(mute_mask, GetTdmOffset(TDMOUT_MUTE3_OFFS));
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

void AmlTdmOutDevice::ConfigTdmSwaps(uint32_t swaps) {
  mmio_.Write32(swaps, GetTdmOffset(TDMOUT_SWAP_OFFS));
}

// Stops the tdm from clocking data out of fifo onto bus
void AmlTdmOutDevice::TdmOutDisable() {
  mmio_.ClearBits32(1 << 31, GetTdmOffset(TDMOUT_CTRL0_OFFS));
}
// Enables the tdm to clock data out of fifo onto bus
void AmlTdmOutDevice::TdmOutEnable() { mmio_.SetBits32(1 << 31, GetTdmOffset(TDMOUT_CTRL0_OFFS)); }

void AmlTdmOutDevice::FRDDREnable() {
  // Set the load bit, will make sure things start from beginning of buffer
  mmio_.SetBits32(1 << 12, GetFrddrOffset(FRDDR_CTRL1_OFFS));
  mmio_.SetBits32(1 << 31, GetFrddrOffset(FRDDR_CTRL0_OFFS));
}

void AmlTdmOutDevice::FRDDRDisable() {
  // Clear the load bit (this is the bit that forces the initial fetch of
  //    start address into current ptr)
  mmio_.ClearBits32(1 << 12, GetFrddrOffset(FRDDR_CTRL1_OFFS));
  // Disable the frddr channel
  mmio_.ClearBits32(1 << 31, GetFrddrOffset(FRDDR_CTRL0_OFFS));
}

void AmlTdmOutDevice::Sync() {
  mmio_.ClearBits32(3 << 28, GetTdmOffset(TDMOUT_CTRL0_OFFS));
  mmio_.SetBits32(1 << 29, GetTdmOffset(TDMOUT_CTRL0_OFFS));
  mmio_.SetBits32(1 << 28, GetTdmOffset(TDMOUT_CTRL0_OFFS));
}

// Resets frddr mechanisms to start at beginning of buffer
//   starts the frddr (this will fill the fifo)
//   starts the tdm to clock out data on the bus
// returns the start time
uint64_t AmlTdmOutDevice::Start() {
  uint64_t a, b;

  Sync();
  FRDDREnable();
  a = zx_clock_get_monotonic();
  TdmOutEnable();
  b = zx_clock_get_monotonic();
  return ((b - a) >> 1) + a;
}

void AmlTdmOutDevice::Stop() {
  TdmOutDisable();
  FRDDRDisable();
}

void AmlTdmOutDevice::Shutdown() {
  Stop();

  // Disable the output signals
  zx_off_t ptr = EE_AUDIO_CLK_TDMOUT_A_CTL + tdm_ch_ * sizeof(uint32_t);
  mmio_.ClearBits32(0x03 << 30, ptr);

  // Disable the audio domain clocks used by this instance.
  AudioClkDis((EE_AUDIO_CLK_GATE_TDMOUTA << tdm_ch_) | (EE_AUDIO_CLK_GATE_FRDDRA << frddr_ch_));

  // Note: We are leaving the ARB unit clocked as well as MCLK and
  //  SCLK generation units since it is possible they are used by
  //  some other audio driver outside of this instance
}
