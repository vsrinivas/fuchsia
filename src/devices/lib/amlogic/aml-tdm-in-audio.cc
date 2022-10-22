// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>

#include <limits>
#include <memory>
#include <utility>

#include <fbl/alloc_checker.h>
#include <soc/aml-common/aml-tdm-audio.h>

// Only enable for testing without configuring loopback in the BT chip.
// Disable metadata::AmlConfig swaps first.
// #define ENABLE_BT_LOOPBACK

// static
std::unique_ptr<AmlTdmDevice> AmlTdmInDevice::Create(fdf::MmioBuffer mmio, ee_audio_mclk_src_t src,
                                                     aml_tdm_in_t tdm, aml_toddr_t toddr,
                                                     aml_tdm_mclk_t mclk,
                                                     metadata::AmlVersion version) {
  // TODDR A has 256 64-bit lines in the FIFO, B and C have 128.
  uint32_t fifo_depth = 128 * 8;  // in bytes.
  switch (version) {
    case metadata::AmlVersion::kS905D2G:
      if (toddr == TODDR_A)
        fifo_depth = 256 * 8;  // TODDR_A has 256 x 64-bit
      else
        fifo_depth = 128 * 8;  // TODDR_B/C has 128 x 64-bit
      break;
    case metadata::AmlVersion::kS905D3G:
      if (toddr == TODDR_A)
        fifo_depth = 4096 * 8;  // TODDR_A has 4096 x 64-bit
      else
        fifo_depth = 128 * 8;  // TODDR_B/C/D has 128 x 64-bit
      break;
    case metadata::AmlVersion::kA5:
      fifo_depth = 64 * 8;  // TODDR_A/B has 64 x 64-bit
      break;
  }

  fbl::AllocChecker ac;
  auto dev = std::unique_ptr<AmlTdmInDevice>(
      new (&ac) AmlTdmInDevice(std::move(mmio), src, tdm, toddr, mclk, fifo_depth, version));
  if (!ac.check()) {
    return nullptr;
  }

  return dev;
}

void AmlTdmInDevice::Initialize() {
  // Enable the audio domain clocks used by this instance.
  AudioClkEna((EE_AUDIO_CLK_GATE_TDMINA << tdm_ch_) | (EE_AUDIO_CLK_GATE_TODDRA << toddr_ch_) |
              EE_AUDIO_CLK_GATE_ARB);

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
      __FALLTHROUGH;
    case metadata::AmlVersion::kS905D2G:
      // Enable DDR ARB, and enable this ddr channels bit.
      mmio_.SetBits32((1 << 31) | (1 << (toddr_ch_)), EE_AUDIO_ARB_CTRL);
      mmio_.Write32((0x30 << 16) |       // Enable interrupts for FIFO errors.
                        (0x00 << 13) |   // Packed.
                        (31 << 8) |      // MSB position of data.
                        (16 << 3) |      // LSB position of data.
                        (tdm_ch_ << 0),  // select TDM_IN A/B/C as data source.
                    GetToddrOffset(TODDR_CTRL0_OFFS));
      mmio_.Write32((1 << 25) |  // set the magic force end bit(25) to cause fetch from start
                        ((fifo_depth_ / 8 / 2) << 16) |  // trigger ddr when fifo half full
                        (0x02 << 8),                     // STATUS2 source is ddr position
                    GetToddrOffset(TODDR_CTRL1_OFFS));
      break;
    case metadata::AmlVersion::kA5:
      mmio_.Write32((0x00 << 13) |   // Packed.
                        (31 << 8) |  // MSB position of data.
                        (16 << 3),   // LSB position of data. - (S/U32 - 0; S/U16 - 16)
                    GetToddrOffset(TODDR_CTRL0_OFFS));
      mmio_.Write32((0x0 << 26) |    // select tdmin_a as data source
                        (1 << 25) |  // set the magic force end bit(25) to cause fetch from start
                        ((fifo_depth_ / 8 / 2) << 12) |  // trigger ddr when fifo half full
                        (0x02 << 8),                     // STATUS2 source is ddr position
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

uint32_t AmlTdmInDevice::GetRingPosition() {
  return mmio_.Read32(GetToddrOffset(TODDR_STATUS2_OFFS)) -
         mmio_.Read32(GetToddrOffset(TODDR_START_ADDR_OFFS));
}

uint32_t AmlTdmInDevice::GetDmaStatus() { return mmio_.Read32(GetToddrOffset(TODDR_STATUS1_OFFS)); }

uint32_t AmlTdmInDevice::GetTdmStatus() { return mmio_.Read32(GetTdmOffset(TDMIN_CTRL_OFFS)); }

zx_status_t AmlTdmInDevice::SetBuffer(zx_paddr_t buf, size_t len) {
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

zx_status_t AmlTdmInDevice::SetSclkPad(aml_tdm_sclk_pad_t sclk_pad, bool is_custom_select) {
  // Datasheets state that PAD_CTRL1 controls sclk and lrclk source selection (which mclk),
  // it does this per pad (0, 1, 2).  These pads are tied to the TDM channel in use
  // According to board layout design, select the right sclk pad, lrclk pad.
  // Note: tdm_ch_ no obvious relationship with clk_pad
  uint32_t pad[2] = {};
  bool pad_ctrl_separated = false;
  switch (version_) {
    case metadata::AmlVersion::kS905D2G:
      pad[0] = EE_AUDIO_MST_PAD_CTRL1;
      break;
    case metadata::AmlVersion::kS905D3G:
      pad[0] = EE_AUDIO_MST_PAD_CTRL1_D3G;
      break;
    case metadata::AmlVersion::kA5:
      pad[0] = EE_AUDIO_SCLK_PAD_CTRL0_A5;
      pad[1] = EE_AUDIO_LRCLK_PAD_CTRL0_A5;
      pad_ctrl_separated = true;
      break;
  }

  aml_tdm_sclk_pad_t spad = {};
  switch (tdm_ch_) {
    case TDM_IN_A:
      spad = SCLK_PAD_0;
      break;
    case TDM_IN_B:
      spad = SCLK_PAD_1;
      break;
    case TDM_IN_C:
      spad = SCLK_PAD_2;
      break;
    case TDM_IN_LB:
      ZX_ASSERT(0);
      break;
  }

  if (is_custom_select)
    spad = sclk_pad;

  // Only modify the part of the MST PAD register that corresponds to the engine in use.
  switch (spad) {
    case SCLK_PAD_0:
      if (pad_ctrl_separated) {
        mmio_.ClearBits32(1 << 3, pad[0]);  // sclk_pad0 as output
        mmio_.ClearBits32(1 << 3, pad[1]);  // lrclk_pad0 as output
        mmio_.ModifyBits32((mclk_ch_ << 0), (7 << 0), pad[0]);
        mmio_.ModifyBits32((mclk_ch_ << 0), (7 << 0), pad[1]);
      } else {
        mmio_.ModifyBits32((mclk_ch_ << 16) | (mclk_ch_ << 0), (7 << 16) | (7 << 0), pad[0]);
      }
      break;
    case SCLK_PAD_1:
      if (pad_ctrl_separated) {
        mmio_.ClearBits32(1 << 7, pad[0]);  // sclk_pad1 as output
        mmio_.ClearBits32(1 << 7, pad[1]);  // lrclk_pad1 as output
        mmio_.ModifyBits32((mclk_ch_ << 4), (7 << 4), pad[0]);
        mmio_.ModifyBits32((mclk_ch_ << 4), (7 << 4), pad[1]);
      } else {
        mmio_.ModifyBits32((mclk_ch_ << 20) | (mclk_ch_ << 4), (7 << 20) | (7 << 4), pad[0]);
      }
      break;
      break;
    case SCLK_PAD_2:
      if (pad_ctrl_separated) {
        mmio_.ClearBits32(1 << 11, pad[0]);  // sclk_pad2 as output
        mmio_.ClearBits32(1 << 11, pad[1]);  // lrclk_pad2 as output
        mmio_.ModifyBits32((mclk_ch_ << 8), (7 << 8), pad[0]);
        mmio_.ModifyBits32((mclk_ch_ << 8), (7 << 8), pad[1]);
      } else {
        mmio_.ModifyBits32((mclk_ch_ << 24) | (mclk_ch_ << 8), (7 << 24) | (7 << 8), pad[0]);
      }
      break;
    default:
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t AmlTdmInDevice::SetDatPad(aml_tdm_dat_pad_t tdm_pin, aml_tdm_dat_lane_t dat_lane) {
  // val - in src sel
  // 0 ~ 31: TDM_D0 ~ TDM_D31
  uint32_t val = tdm_pin;

  // CTRL0: tdmina lane3 ~ 0
  // CTRL1: tdmina lane7 ~ 4
  // CTRL2: tdminb lane3 ~ 0
  // CTRL3: tdminb lane7 ~ 4
  // CTRL4: tdminc lane3 ~ 0
  // CTRL5: tdminc lane7 ~ 4
  zx_off_t ptr = EE_AUDIO_DAT_PAD_CTRL0_A5 + tdm_ch_ / 4 * sizeof(uint32_t);

  if (version_ != metadata::AmlVersion::kA5)
    return ZX_OK;

  switch (dat_lane) {
    case LANE_0:
    case LANE_4:
      mmio_.ModifyBits32(val << 0, 0x1f << 0, ptr);
      break;
    case LANE_1:
    case LANE_5:
      mmio_.ModifyBits32(val << 8, 0x1f << 8, ptr);
      break;
    case LANE_2:
    case LANE_6:
      mmio_.ModifyBits32(val << 16, 0x1f << 16, ptr);
      break;
    case LANE_3:
    case LANE_7:
      mmio_.ModifyBits32(val << 24, 0x1f << 24, ptr);
      break;
  }

  // oen val: 0 - output; 1 - input;
  // bit[31:0] - D31 ~ D0
  mmio_.SetBits32(1 << tdm_pin, EE_AUDIO_DAT_PAD_CTRLF_A5);
  return ZX_OK;
}

// bit_offset - bit position in frame where first slot will appear.
// num_slots - number of slots per frame minus one.
// bits_per_slot - width of each slot minus one.
// bits_per_sample - number of bits in sample minus one.
// mix_mask - lanes to mix L+R.
void AmlTdmInDevice::ConfigTdmSlot(uint8_t bit_offset, uint8_t num_slots, uint8_t bits_per_slot,
                                   uint8_t bits_per_sample, uint8_t mix_mask, bool i2s_mode) {
  switch (version_) {
    case metadata::AmlVersion::kS905D3G:
      __FALLTHROUGH;
    case metadata::AmlVersion::kS905D2G: {
      uint32_t src = 0;
      switch (tdm_ch_) {
        case TDM_IN_A:
          src = 0;
          break;
        case TDM_IN_B:
          src = 1;
          break;
        case TDM_IN_C:
          src = 2;
          break;
        case TDM_IN_LB:
          ZX_ASSERT(0);
          break;
      }
#ifdef ENABLE_BT_LOOPBACK
      src += 3;
#endif
      uint32_t reg0 = (i2s_mode << 30) |    // TDM/I2S mode.
                      (src << 20) |         // Source for TDMIN.
                      (bit_offset << 16) |  // Add delay to ws or data for skew modification.
                      bits_per_slot;
      mmio_.Write32(reg0, GetTdmOffset(TDMIN_CTRL_OFFS));
    } break;
    case metadata::AmlVersion::kA5: {
      uint32_t src = 0;
      switch (tdm_ch_) {
        case TDM_IN_A:
          src = 0;
          break;
        case TDM_IN_B:
          src = 1;
          break;
        case TDM_IN_C:
          src = 2;
          break;
        case TDM_IN_LB:
          ZX_ASSERT(0);
          break;
      }
      uint32_t reg0 = (i2s_mode << 30) |    // TDM/I2S mode.
                      (1 << 26) |           // Enable TDMIN resync for signal stable.
                      (src << 20) |         // Source for TDMIN.
                      (bit_offset << 16) |  // Add delay to ws or data for skew modification.
                      bits_per_slot;
      reg0 |= (i2s_mode) ? (0x1 << 8) :  // I2S: fixed - 0x1
                  (num_slots << 8);      // TDM: set 31 which mean 32 slots

      mmio_.Write32(reg0, GetTdmOffset(TDMIN_CTRL_OFFS));
    } break;
  }
}

zx_status_t AmlTdmInDevice::ConfigTdmLane(size_t lane, uint32_t enable_mask, uint32_t mute_mask) {
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

void AmlTdmInDevice::ConfigTdmSwaps(uint32_t swaps) {
  mmio_.Write32(swaps, GetTdmOffset(TDMIN_SWAP_OFFS));
}

// Stops the tdm from clocking data out of fifo onto bus
void AmlTdmInDevice::TdmInDisable() { mmio_.ClearBits32(1 << 31, GetTdmOffset(TDMIN_CTRL_OFFS)); }
// Enables the tdm to clock data out of fifo onto bus
void AmlTdmInDevice::TdmInEnable() { mmio_.SetBits32(1 << 31, GetTdmOffset(TDMIN_CTRL_OFFS)); }

void AmlTdmInDevice::TODDREnable() { mmio_.SetBits32(1 << 31, GetToddrOffset(TODDR_CTRL0_OFFS)); }

void AmlTdmInDevice::TODDRDisable() {
  // Disable the toddr channel
  mmio_.ClearBits32(1 << 31, GetToddrOffset(TODDR_CTRL0_OFFS));
}

void AmlTdmInDevice::Sync() {
  mmio_.ClearBits32(3 << 28, GetTdmOffset(TDMIN_CTRL_OFFS));
  mmio_.SetBits32(1 << 29, GetTdmOffset(TDMIN_CTRL_OFFS));
  mmio_.SetBits32(1 << 28, GetTdmOffset(TDMIN_CTRL_OFFS));
}

// Resets toddr mechanisms to start at beginning of buffer
//   starts the toddr (this will fill the fifo)
//   starts the tdm to clock out data on the bus
// returns the start time
uint64_t AmlTdmInDevice::Start() {
  uint64_t a, b;

  Sync();
  TODDREnable();
  a = zx_clock_get_monotonic();
  TdmInEnable();
  b = zx_clock_get_monotonic();
  return ((b - a) >> 1) + a;
}

void AmlTdmInDevice::Stop() {
  TdmInDisable();
  TODDRDisable();
}

void AmlTdmInDevice::Shutdown() {
  Stop();

  // Disable the output signals
  zx_off_t ptr = EE_AUDIO_CLK_TDMIN_A_CTL + tdm_ch_ * sizeof(uint32_t);
  mmio_.ClearBits32(0x03 << 30, ptr);

  // Disable the audio domain clocks used by this instance.
  AudioClkDis((EE_AUDIO_CLK_GATE_TDMINA << tdm_ch_) | (EE_AUDIO_CLK_GATE_TODDRA << toddr_ch_));

  // Note: We are leaving the ARB unit clocked as well as MCLK and
  //  SCLK generation units since it is possible they are used by
  //  some other audio driver outside of this instance
}
