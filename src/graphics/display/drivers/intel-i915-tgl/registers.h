// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_H_

#include <zircon/assert.h>

#include <hwreg/bitfields.h>

#include "src/graphics/display/drivers/intel-i915-tgl/registers-ddi.h"

namespace tgl_registers {

// Graphics & Memory Controller Hub Graphics Control - GGC_0_0_0_PCI
class GmchGfxControl : public hwreg::RegisterBase<GmchGfxControl, uint16_t> {
 public:
  static constexpr uint32_t kAddr = 0x50;  // Address for the mirror

  DEF_FIELD(15, 8, gfx_mode_select);
  DEF_FIELD(7, 6, gtt_size);

  inline uint32_t gtt_mappable_mem_size() { return gtt_size() ? 1 << (20 + gtt_size()) : 0; }

  inline uint32_t dsm_size() {
    if (gfx_mode_select() <= 0x10) {
      return gfx_mode_select() * 32 * 1024 * 1024;
    } else if (gfx_mode_select() == 0x20) {
      return 1024 * 1024 * 1024;
    } else if (gfx_mode_select() == 0x30) {
      return 1536 * 1024 * 1024;
    } else if (gfx_mode_select() == 0x40) {
      return 2048 * 1024 * 1024u;
    } else if (0xf0 <= gfx_mode_select() && gfx_mode_select() < 0xff) {
      return (gfx_mode_select() - 0xef) * 4 * 1024 * 1024;
    } else {
      return 0;
    }
  }

  static auto Get() { return hwreg::RegisterAddr<GmchGfxControl>(0); }
};

// Base Data of Stolen Memory - BDSM_0_0_0_PCI
class BaseDsm : public hwreg::RegisterBase<BaseDsm, uint32_t> {
 public:
  static constexpr uint32_t kAddr = 0x5c;  // Address for the mirror

  DEF_FIELD(31, 20, base_phys_addr);
  static constexpr uint32_t base_phys_addr_shift = 20;
  DEF_RSVDZ_FIELD(19, 1);
  DEF_BIT(0, lock);

  static auto Get() { return hwreg::RegisterAddr<BaseDsm>(0); }
};

// DFSM (Display Fuse)
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-12.21 Part 1 pages 432-434
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 pages 497-499
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 pages 495-497
class DisplayFuses : public hwreg::RegisterBase<DisplayFuses, uint32_t> {
 public:
  enum class CoreClockLimit : int {
    k675Mhz = 0,
    k540Mhz = 1,
    k450Mhz = 2,
    k337_5Mhz = 3,
  };

  // The registers names here use "_enabled" / "_disabled" prefixes
  // inconsistently in order to reflect the semantics used in the hardware.

  // Not on Tiger Lake.
  DEF_BIT(31, graphics_disabled);

  DEF_BIT(30, pipe_a_disabled);
  DEF_BIT(28, pipe_c_disabled);
  // FBC (Frame Buffer Compression) and DPST (Display Power Savings Technology).
  DEF_BIT(27, power_management_disabled);

  // Tiger Lake: All combo PHY ports disabled.
  // Kaby Lake and Skylake: DDIA eDP support disabled.
  DEF_BIT(26, edp_disabled);

  // Not on Tiger Lake.
  DEF_FIELD(24, 23, core_clock_limit_bits);
  CoreClockLimit GetCoreClockLimit() const {
    return static_cast<CoreClockLimit>(core_clock_limit_bits());
  }

  // Only Tiger Lake.
  DEF_BIT(22, pipe_d_disabled);

  DEF_BIT(21, pipe_b_disabled);
  // Display capture is called WD (Wireless Display) in Intel docs.
  DEF_BIT(20, display_capture_disabled);

  // Only Tiger Lake.
  DEF_BIT(16, isolated_decode_disabled);
  DEF_FIELD(15, 8, audio_codec_id_low);
  DEF_BIT(7, display_stream_compression_disabled);

  DEF_BIT(6, remote_screen_blanking_enabled);
  DEF_BIT(0, audio_codec_disabled);

  static auto Get() { return hwreg::RegisterAddr<DisplayFuses>(0x51000); }
};

// DSSM (Display Strap State)
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-12.21 Part 1 pages 825-827
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 pages 545-546
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 pages 545-546
//
// This register is based on the Tiger Lake definition.
class Dssm : public hwreg::RegisterBase<Dssm, uint32_t> {
 public:
  enum class RefFrequency : uint8_t {
    k24Mhz = 0,
    k19_2Mhz = 1,
    k38_4Mhz = 2,
  };
  DEF_FIELD(31, 29, ref_frequency);
  RefFrequency GetRefFrequency() const { return static_cast<RefFrequency>(ref_frequency()); }

  static auto Get() { return hwreg::RegisterAddr<Dssm>(0x51004); }
};

// DISPLAY_INT_CTL (ICL+), a.k.a. MASTER_INT_CTL (SKL)
class DisplayInterruptControl : public hwreg::RegisterBase<DisplayInterruptControl, uint32_t> {
 public:
  DEF_BIT(31, enable_mask);
  DEF_BIT(23, sde_int_pending);
  DEF_BIT(21, de_hpd_int_pending);
  DEF_BIT(18, de_pipe_c_int_pending);
  DEF_BIT(17, de_pipe_b_int_pending);
  DEF_BIT(16, de_pipe_a_int_pending);

  static auto Get() { return hwreg::RegisterAddr<DisplayInterruptControl>(0x44200); }
};

// GFX_MSTR_INTR (gen11)
class GfxMasterInterrupt : public hwreg::RegisterBase<GfxMasterInterrupt, uint32_t> {
 public:
  DEF_BIT(31, primary_interrupt);
  DEF_BIT(16, display);
  DEF_BIT(1, gt1);
  DEF_BIT(0, gt0);

  static auto Get() { return hwreg::RegisterAddr<GfxMasterInterrupt>(0x190010); }
};

// GMBUS0
class GMBus0 : public hwreg::RegisterBase<GMBus0, uint32_t> {
 public:
  DEF_FIELD(2, 0, pin_pair_select);
  static constexpr uint32_t kDdiCPin = 4;
  static constexpr uint32_t kDdiBPin = 5;
  static constexpr uint32_t kDdiDPin = 6;

  static auto Get() { return hwreg::RegisterAddr<GMBus0>(0xc5100); }
};

// GMBUS1
class GMBus1 : public hwreg::RegisterBase<GMBus1, uint32_t> {
 public:
  DEF_BIT(31, sw_clear_int);
  DEF_BIT(30, sw_ready);
  DEF_BIT(27, bus_cycle_stop);
  DEF_BIT(25, bus_cycle_wait);
  DEF_FIELD(24, 16, total_byte_count);
  DEF_FIELD(7, 1, slave_register_addr);
  DEF_BIT(0, read_op);

  static auto Get() { return hwreg::RegisterAddr<GMBus1>(0xc5104); }
};

// GMBUS2
class GMBus2 : public hwreg::RegisterBase<GMBus2, uint32_t> {
 public:
  DEF_BIT(14, wait);
  DEF_BIT(11, hw_ready);
  DEF_BIT(10, nack);
  DEF_BIT(9, active);

  static auto Get() { return hwreg::RegisterAddr<GMBus2>(0xc5108); }
};

// GMBUS3
class GMBus3 : public hwreg::RegisterBase<GMBus3, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<GMBus3>(0xc510c); }
};

// GMBUS4
class GMBus4 : public hwreg::RegisterBase<GMBus4, uint32_t> {
 public:
  DEF_FIELD(4, 0, interrupt_mask);

  static auto Get() { return hwreg::RegisterAddr<GMBus4>(0xc5110); }
};

// PWR_WELL_CTL
//
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 690-693
// Tiger Lake: IHD-OS-TGL-Vol 2c-12.21 Part 2 pages 1063-1065
class PowerWellControl : public hwreg::RegisterBase<PowerWellControl, uint32_t> {
 public:
  hwreg::BitfieldRef<uint32_t> power_request(size_t index) {
    ZX_DEBUG_ASSERT(index & 1);
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), index, index);
  }

  hwreg::BitfieldRef<uint32_t> power_state(size_t index) {
    ZX_DEBUG_ASSERT((index & 1) == 0);
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), index, index);
  }

  hwreg::BitfieldRef<uint32_t> ddi_io_power_request_skylake(Ddi ddi) {
    int bit = 2 + ((ddi == DDI_A || ddi == DDI_E) ? 0 : ddi * 2) + 1;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> ddi_io_power_state_skylake(Ddi ddi) {
    int bit = 2 + ((ddi == DDI_A || ddi == DDI_E) ? 0 : ddi * 2);
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  // Misc IO Power Request (on Skylake / Kaby Lake only)
  // This field requests power for Miscellaneous IO to enable (1) or disable (0).
  // This includes all AUX channels, audio pins, and utility pin.
  hwreg::BitfieldRef<uint32_t> misc_io_power_request_skylake() {
    int bit = kMiscIoBitsOffset + 1;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  // Misc IO Power State (on Skylake / Kaby Lake only):
  // Enabled (1) or disabled (0).
  hwreg::BitfieldRef<uint32_t> misc_io_power_state_skylake() {
    int bit = kMiscIoBitsOffset;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  static auto Get() {
    // The address below is for PWR_WELL_CTL2, which is provided for driver use.
    // By contrast, PWR_WELL_CTL1 is intended for BIOS use.
    return hwreg::RegisterAddr<PowerWellControl>(0x45404);
  }

 private:
  constexpr static size_t kMiscIoBitsOffset = 0;
};

// PWR_WELL_CTL_AUX2 (Power Well Control AUX2)
// Control display power for AUX IO for each DDI / Transport.
// This register is only available on Tiger Lake.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 1072-1077
class PowerWellControlAux : public hwreg::RegisterBase<PowerWellControlAux, uint32_t> {
 public:
  DEF_BIT(29, power_on_request_thunderbolt_6);
  DEF_BIT(28, powered_on_thunderbolt_6);

  DEF_BIT(27, power_on_request_thunderbolt_5);
  DEF_BIT(26, powered_on_thunderbolt_5);

  DEF_BIT(25, power_on_request_thunderbolt_4);
  DEF_BIT(24, powered_on_thunderbolt_4);

  DEF_BIT(23, power_on_request_thunderbolt_3);
  DEF_BIT(22, powered_on_thunderbolt_3);

  DEF_BIT(21, power_on_request_thunderbolt_2);
  DEF_BIT(20, powered_on_thunderbolt_2);

  DEF_BIT(19, power_on_request_thunderbolt_1);
  DEF_BIT(18, powered_on_thunderbolt_1);

  DEF_BIT(17, power_on_request_usb_c_6);
  DEF_BIT(16, powered_on_usb_c_6);

  DEF_BIT(15, power_on_request_usb_c_5);
  DEF_BIT(14, powered_on_usb_c_5);

  DEF_BIT(13, power_on_request_usb_c_4);
  DEF_BIT(12, powered_on_usb_c_4);

  DEF_BIT(11, power_on_request_usb_c_3);
  DEF_BIT(10, powered_on_usb_c_3);

  DEF_BIT(9, power_on_request_usb_c_2);
  DEF_BIT(8, powered_on_usb_c_2);

  DEF_BIT(7, power_on_request_usb_c_1);
  DEF_BIT(6, powered_on_usb_c_1);

  DEF_BIT(5, power_on_request_c);
  DEF_BIT(4, powered_on_c);

  DEF_BIT(3, power_on_request_b);
  DEF_BIT(2, powered_on_b);

  DEF_BIT(1, power_on_request_a);
  DEF_BIT(0, powered_on_a);

  SelfType& set_power_on_request_combo_or_usb_c(Ddi ddi, bool enabled) {
    ZX_DEBUG_ASSERT(ddi >= DDI_A);
    ZX_DEBUG_ASSERT(ddi <= DDI_TC_6);
    const uint32_t bit = ddi * 2 + 1;
    hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit).set(enabled);
    return *this;
  }

  bool powered_on_combo_or_usb_c(Ddi ddi) const {
    ZX_DEBUG_ASSERT(ddi >= DDI_A);
    ZX_DEBUG_ASSERT(ddi <= DDI_TC_6);
    const uint32_t bit = ddi * 2;
    return hwreg::BitfieldRef<const uint32_t>(reg_value_ptr(), bit, bit).get();
  }

  SelfType& set_power_on_request_thunderbolt(Ddi ddi, bool enabled) {
    ZX_DEBUG_ASSERT(ddi >= DDI_TC_1);
    ZX_DEBUG_ASSERT(ddi <= DDI_TC_6);
    // The request_thunderbolt_* bits start from bit 19.
    const uint32_t bit = (ddi - DDI_TC_1) * 2 + 19;
    hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit).set(enabled);
    return *this;
  }

  bool powered_on_thunderbolt(Ddi ddi) const {
    ZX_DEBUG_ASSERT(ddi >= DDI_TC_1);
    ZX_DEBUG_ASSERT(ddi <= DDI_TC_6);
    // The state_thunderbolt_* bits start from bit 18.
    const uint32_t bit = (ddi - DDI_TC_1) * 2 + 18;
    return hwreg::BitfieldRef<const uint32_t>(reg_value_ptr(), bit, bit).get();
  }

  static auto Get() {
    // The address below is for PWR_WELL_CTL_AUX2, which is provided for driver
    // use. By contrast, PWR_WELL_CTL_AUX1 is intended for BIOS use.
    return hwreg::RegisterAddr<PowerWellControlAux>(0x45444);
  }
};

// PWR_WELL_CTL_DDI
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-12.21 Part 2 pages 1072-1075
class PowerWellControlDdi2 : public hwreg::RegisterBase<PowerWellControlDdi2, uint32_t> {
 public:
  hwreg::BitfieldRef<uint32_t> ddi_io_power_request_tiger_lake(Ddi ddi) {
    // DDI A-C: bits 1/3/5. DDI TC1-6: bits 7/9/11/13/15/17.
    int bit = ddi * 2 + 1;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> ddi_io_power_state_tiger_lake(Ddi ddi) {
    int bit = ddi * 2;  // DDI A-C: bits 0/2/4. DDI TC1-6: bits 6/8/10/12/14/16.
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  static auto Get() {
    // The address below is for PWR_WELL_CTL_DDI2, which is provided for driver
    // use. By contrast, PWR_WELL_CTL_DDI1 is intended for BIOS use.
    return hwreg::RegisterAddr<PowerWellControlDdi2>(0x45454);
  }
};

// FUSE_STATUS
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-12.21 Part 1 pages 990-991
class FuseStatus : public hwreg::RegisterBase<FuseStatus, uint32_t> {
 public:
  DEF_BIT(31, fuse_download_status);
  DEF_BIT(27, pg0_dist_status);
  DEF_BIT(26, pg1_dist_status);
  DEF_BIT(25, pg2_dist_status);
  DEF_BIT(24, pg3_dist_status);  // Only for Icy lake or higher gen
  DEF_BIT(23, pg4_dist_status);  // Only for Icy lake or higher gen
  DEF_BIT(22, pg5_dist_status);  // Only for Tiger lake or higher gen

  uint32_t dist_status(size_t index) {
    ZX_DEBUG_ASSERT(index >= 0 && index <= 31);
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), index, index).get();
  }

  static auto Get() { return hwreg::RegisterAddr<FuseStatus>(0x42000); }
};

// NDE_RSTWRN_OPT (North Display Reset Warn Options)
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 134
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 141
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 440
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 436
//
// This register has at least 1 bit that is reserved but not MBZ. The only safe
// way to modify it is via quick read-modify-write operations.
class DisplayResetOptions : public hwreg::RegisterBase<DisplayResetOptions, uint32_t> {
 public:
  // If true, the North Display Engine will notify PCH display engine when it is
  // reset, and will wait for the PCH display engine to acknowledge the reset.
  //
  // The display engine initialization sequence involves setting this to true.
  DEF_BIT(4, pch_reset_handshake);

  static auto Get() { return hwreg::RegisterAddr<DisplayResetOptions>(0x46408); }
};

// AUD_EDID_DATA
class AudEdidData : public hwreg::RegisterBase<AudEdidData, uint32_t> {
 public:
  DEF_FIELD(31, 0, data);

  static auto Get(int transcoder_id) {
    if (transcoder_id == 0) {
      return hwreg::RegisterAddr<AudEdidData>(0x65050);
    } else if (transcoder_id == 1) {
      return hwreg::RegisterAddr<AudEdidData>(0x65150);
    } else {  // transcoder_id == 2.
      ZX_DEBUG_ASSERT(transcoder_id == 2);
      return hwreg::RegisterAddr<AudEdidData>(0x65250);
    }
  }
};

// AUD_DIP_ELD_CTRL_ST
class AudioDipEldControlStatus : public hwreg::RegisterBase<AudioDipEldControlStatus, uint32_t> {
 public:
  DEF_FIELD(30, 29, dip_port_select);
  DEF_FIELD(24, 21, dip_type_enable_status);
  DEF_FIELD(20, 18, dip_buffer_index);
  DEF_FIELD(17, 16, dip_transmission_frequency);
  DEF_FIELD(14, 10, eld_buffer_size);
  DEF_FIELD(9, 5, eld_access_address);
  DEF_BIT(4, eld_ack);
  DEF_FIELD(3, 0, dip_access_address);
  static auto Get() { return hwreg::RegisterAddr<AudioDipEldControlStatus>(0x650B4); }
};

// AUD_PIN_ELD_CP_VLD
class AudioPinEldCPReadyStatus : public hwreg::RegisterBase<AudioPinEldCPReadyStatus, uint32_t> {
 public:
  DEF_BIT(11, audio_inactive_c);
  DEF_BIT(10, audio_enable_c);
  DEF_BIT(9, cp_ready_c);
  DEF_BIT(8, eld_valid_c);

  DEF_BIT(7, audio_inactive_b);
  DEF_BIT(6, audio_enable_b);
  DEF_BIT(5, cp_ready_b);
  DEF_BIT(4, eld_valid_b);

  DEF_BIT(3, audio_inactive_a);
  DEF_BIT(2, audio_enable_a);
  DEF_BIT(1, cp_ready_a);
  DEF_BIT(0, eld_valid_a);

  static auto Get() { return hwreg::RegisterAddr<AudioPinEldCPReadyStatus>(0x650C0); }
};

// CDCLK_CTL (CD Clock Control)
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-12.21 Part 1 pages 220-222
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 pages 328-329
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 pages 325-326
class CdClockCtl : public hwreg::RegisterBase<CdClockCtl, uint32_t> {
 public:
  DEF_FIELD(27, 26, skl_cd_freq_select);
  static constexpr uint32_t kFreqSelect4XX = 0;
  static constexpr uint32_t kFreqSelect540 = 1;
  static constexpr uint32_t kFreqSelect3XX = 2;
  static constexpr uint32_t kFreqSelect6XX = 3;

  DEF_FIELD(23, 22, icl_cd2x_divider_select);
  static constexpr uint32_t kCd2xDivider1 = 0;
  static constexpr uint32_t kCd2xDivider2 = 1;

  DEF_FIELD(21, 19, icl_cd2x_pipe_select);

  DEF_FIELD(10, 0, cd_freq_decimal);
  // This returns binary representation of CD clock frequency (MHz) in
  // U10.1 format (cd_freq_decimal field). To calculate its value, we first
  // round the frequency to 0.5MHz and then minus it by one.
  static constexpr uint32_t FreqDecimal(uint32_t khz) {
    // Truncate the frequency to 0.25MHz for rounding.
    uint32_t mhz_4x_trunc = khz / 250;
    // Round the frequency to 0.5 MHz.
    uint32_t mhz_2x_round = (mhz_4x_trunc + 1) / 2;
    // Return rounded value minus 1 (0x2 in U10.1 format).
    return mhz_2x_round - 2;
  }

  static auto Get() { return hwreg::RegisterAddr<CdClockCtl>(0x46000); }
};

// CDCLK_PLL_ENABLE on ICL+
class IclCdClkPllEnable : public hwreg::RegisterBase<IclCdClkPllEnable, uint32_t> {
 public:
  DEF_BIT(31, pll_enable);
  DEF_BIT(30, pll_lock);
  DEF_FIELD(7, 0, pll_ratio);

  static auto Get() { return hwreg::RegisterAddr<IclCdClkPllEnable>(0x46070); }
};

// DBUF_CTL
class DbufCtl : public hwreg::RegisterBase<DbufCtl, uint32_t> {
 public:
  DEF_BIT(31, power_request);
  DEF_BIT(30, power_state);

  static auto GetForSlice(size_t slice) {
    ZX_DEBUG_ASSERT(slice >= 1 && slice <= 2);
    switch (slice) {
      case 1:
        return hwreg::RegisterAddr<DbufCtl>(0x45008);
      case 2:
        return hwreg::RegisterAddr<DbufCtl>(0x44fe8);
      default:
        ZX_ASSERT(false);
    }
  }
};

// VGA_CONTROL
class VgaCtl : public hwreg::RegisterBase<VgaCtl, uint32_t> {
 public:
  DEF_BIT(31, vga_display_disable);

  static auto Get() { return hwreg::RegisterAddr<VgaCtl>(0x41000); }
};

// GPIO_CTL
class GpioCtl : public hwreg::RegisterBase<GpioCtl, uint32_t> {
 public:
  DEF_BIT(12, data_in);
  DEF_BIT(11, data_out);
  DEF_BIT(10, data_mask);
  DEF_BIT(9, data_direction_val);
  DEF_BIT(8, data_direction_mask);

  DEF_BIT(4, clock_in);
  DEF_BIT(3, clock_out);
  DEF_BIT(2, clock_mask);
  DEF_BIT(1, clock_direction_val);
  DEF_BIT(0, clock_direction_mask);

  static auto Get(tgl_registers::Ddi ddi) {
    if (ddi == tgl_registers::DDI_B) {
      return hwreg::RegisterAddr<GpioCtl>(0xc5020);
    } else if (ddi == tgl_registers::DDI_C) {
      return hwreg::RegisterAddr<GpioCtl>(0xc501c);
    } else {  // ddi == tgl_registers::DDI_D
      ZX_DEBUG_ASSERT(ddi == tgl_registers::DDI_D);
      return hwreg::RegisterAddr<GpioCtl>(0xc5024);
    }
  }
};

// SBLC_PWM_CTL1 (South / PCH Backlight Control 1)
//
// Not directly documented for DG1, but mentioned in IHD-OS-DG1-Vol 12-2.21
// "Backlight Enabling Sequence" page 349.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 1154
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 787
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 772
class PchBacklightControl : public hwreg::RegisterBase<PchBacklightControl, uint32_t> {
 public:
  // Enables the PWM counter logic. When disabled, the PWM is always inactive.
  DEF_BIT(31, pwm_counter_enabled);

  DEF_RSVDZ_BIT(30);

  // Inverts whether the backlight PWM active duty drives the PWM pin high/low.
  //
  // When 0 (default), the backlight PWM pin is driven high when the PWM is in
  // active duty, and the pin is driven low when the PWM is inactive.
  //
  // When 1 (inverted), the backlight PWM pin is driven low when the PWM is in
  // active duty, and the pin is driven high when the PWM is inactive.
  DEF_BIT(29, pwm_polarity_inverted);

  DEF_RSVDZ_FIELD(28, 0);

  static auto Get() { return hwreg::RegisterAddr<PchBacklightControl>(0xc8250); }

  // Tiger Lake has another instance for a 2nd backlight at 0xc8350.
};

// SBLC_PWM_CTL2 (South / PCH Backlight Control 2)
//
// Does not exist on DG1 or Tiger Lake. The MMIO address is recycled for the new
// SLBC_PWM_FREQ register. The PWM frequency and duty cycle are specified in the
// SLBC_PWM_FREQ and SLBC_PWM_DUTY registers.
//
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 788
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 773
class PchBacklightFreqDuty : public hwreg::RegisterBase<PchBacklightFreqDuty, uint32_t> {
 public:
  // Based on the frequency of the clock and desired PWM frequency.
  //
  // PWM frequency = RAWCLK_FREQ / (freq_divider * backlight_pwm_multiplier)
  // backlight_pwm_multiplier is 16 or 128, based on SCHICKEN_1.
  DEF_FIELD(31, 16, freq_divider);

  // Must be <= `freq_divider`.
  // 0 = 0% PWM duty cycle. `freq_divider` = 100% PWM duty cycle.
  DEF_FIELD(15, 0, duty_cycle);

  static auto Get() { return hwreg::RegisterAddr<PchBacklightFreqDuty>(0xc8254); }
};

// SLBC_PWM_FREQ (South / PCH Backlight PWM Frequency)
//
// Does not exist on Kaby Lake and Skylake. PWM frequency and duty cycle are
// specified in the SBLC_PWM_CTL2 register.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 1156
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 1205
class PchBacklightFreq : public hwreg::RegisterBase<PchBacklightFreq, uint32_t> {
 public:
  // (Reference clock frequency from RAWCLK_FREQ) / (Desired PWM frequency).
  DEF_FIELD(31, 0, divider);

  static auto Get() { return hwreg::RegisterAddr<PchBacklightFreq>(0xc8254); }

  // Tiger Lake has another instance for a 2nd backlight at 0xc8354.
};

// SBLC_PWM_DUTY (South / PCH Backlight PWM Duty Cycle)
//
// Does not exist on Kaby Lake and Skylake. PWM frequency and duty cycle are
// specified in the SBLC_PWM_CTL2 register.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 1155
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 1205
class PchBacklightDuty : public hwreg::RegisterBase<PchBacklightDuty, uint32_t> {
 public:
  // Specified a scale from 0 (0%) to SBLC_PWM_FREQ (100%).
  // Must not exceed SBLC_PWM_FREQ.
  DEF_FIELD(31, 0, value);

  static auto Get() { return hwreg::RegisterAddr<PchBacklightDuty>(0xc8258); }

  // Tiger Lake has another instance for a 2nd backlight at 0xc8358.
};

// SCHICKEN_1 (South / PCH Display Engine Chicken 1)
//
// This register is not explicitly documented, but the Kaby Lake PRMs have clues
// that reveal its name and address.
// * IHD-OS-KBL-Vol 2c-1.17 Part 2 page 788 mentions the SCHICKEN_1 name, and
//   that bit 0 selects between a multiplier of 16 and 128 for SBLC_PWM_CTL2
//   backlight modulation and duty cycle.
// * IHD-OS-KBL-Vol 12-1.17 "Backlight Enabling Sequence" page 197 states that
//   a granularity of 16 or 128 is set in bit 0 of the 0xC2000 register.
//
// The name is a reference to "chicken bits", which are configuration bits that
// create the option to reverse (chicken out of) risky design changes (fixes or
// new features). The risk can be due to the complexity of the feature, or due
// to having to make changes late in the design cycle. More details in
// "Formal Verification - An Essential Toolkit for Modern VLSI Design".
class PchChicken1 : public hwreg::RegisterBase<PchChicken1, uint32_t> {
 public:
  // All bits must be set to 1 on DG1.
  //
  // Setting the bits to 1 compensates for the fact that DG1's HPD signals are
  // inverted (0 = connected, 1 = disconnected). This issue is not mentioned in
  // other PRMs.
  //
  // DG1: IHD-OS-DG1-Vol 12-2.21 "Hotplug Board Inversion" page 352 and
  //      IHD-OS-DG1-Vol 2c-2.21 Part 2 page 1259
  DEF_FIELD(18, 15, hpd_invert_bits);

  // Set on S0ix entry and cleared on S0ix exit.
  //
  // This bit works around an issue bug where the PCH display engine's clock
  // is not stopped when entering the S0ix state. This issue is mentioned in the
  // PRMs listed below.
  //
  // Lakefield: IHD-OS-LKF-Vol 14-4.21 page 15
  // Tiger Lake: IHD-OS-TGL-Vol 14-12.21 page 18 and page 50
  // Ice Lake: IHD-OS-ICLLP-Vol 14-1.20 page 33
  // Not mentioned in DG1, Kaby Lake, or Skylake.
  DEF_BIT(7, pch_display_clock_disable);

  // Toggles shared IO pins between multi-chip genlock and backlight 2.
  //
  // Lake Field: IHD-OS-LKF-Vol 12-4.21 page 50
  // DG1: IHD-OS-DG1-Vol 12-2.21 page 349
  // Kaby Lake and Skylake don't support multi-chip genlock.
  DEF_BIT(2, genlock_instead_of_backlight);

  // Multiplier for the backlight PWM frequency and duty cycle.
  //
  // This multiplier applies to SBLC_PWM_CTL1 and SBLC_PWM_CTL2. It is not
  // present on DG1, where the PWM frequency and duty cycle are specified as
  // 32-bit values in the SBLC_PWM_FREQ and SBLC_PWM_DUTY registers.
  //
  // The multiplier can be 16 (0) or 128 (1).
  //
  // Kaby Lake: IHD-OS-KBL-Vol 12-1.17 "Backlight Enabling Sequence" page 197
  // Skylake: IHD-OS-SKL-Vol 12-05.16 "Backlight Enabling Sequence" page 189
  // Does not exist on DG1.
  DEF_BIT(0, backlight_pwm_multiplier);

  static auto Get() { return hwreg::RegisterAddr<PchChicken1>(0xc2000); }
};

// RAWCLK_FREQ (Rawclk frequency)
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 1083-1084
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 1131-1132
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 712
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 705
class PchRawClock : public hwreg::RegisterBase<PchRawClock, uint32_t> {
 public:
  // The raw clock frequency in MHz. Complex representation used by DG1.
  //
  // Raw clock frequency = integral frequency + fractional frequency
  // Integral frequency = `integer` + 1
  // Fractional frequency = `fraction_numerator` / (`fraction_denominator` + 1)
  //
  // `fraction_denominator` must be zero if `fraction_numerator` is zero.
  // Only `fraction_numerator` values 0-2 are documented as supported.
  //
  // All these fields must be zero on Kaby Lake and Skylake.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 1083-1084
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 1131-1132
  DEF_FIELD(29, 26, fraction_denominator);
  DEF_FIELD(25, 16, integer);
  DEF_FIELD(13, 11, fraction_numerator);

  // The raw clock frequency in MHz.
  //
  // This must be set to 24MHz on Kaby Lake and Skylake. Must be zero on Tiger
  // Lake and DG1.
  //
  // Kaby Lake: IHD-OS-KBL-Vol 12-1.17 page 195
  // Skylake: IHD-OS-SKL-Vol 12-05.16 page 188
  DEF_FIELD(9, 0, mhz);

  static auto Get() { return hwreg::RegisterAddr<PchRawClock>(0xc6204); }
};

// PP_CONTROL (Panel Power Control)
//
// The Tiger Lake PRMS do not include a description for this register. However,
// IHD-OS-TGL-Vol 14-12.21 pages 29 and 56 mention the register name and
// address. Experiments on Tiger Lake (device ID 0x9a49) suggest that this
// register has the same semantics as in DG1.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 961-962
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 986-987
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 pages 626-627
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 pages 620-621
class PchPanelPowerControl : public hwreg::RegisterBase<PchPanelPowerControl, uint32_t> {
 public:
  // eDP T12 - Required delay from panel power disable to power enable.
  //
  // Value = (desired_delay / 100ms) + 1.
  // Zero means no delay, and also stops a current delay.
  //
  // Must be zero on Kaby Lake and Skylake.
  DEF_FIELD(8, 4, power_cycle_delay);

  // If true, the eDP port's VDD is on even if the panel power sequence hasn't
  // been completed. Intended for panels that need VDD for DP AUX transactions.
  //
  // This setting overrides all power sequencing logic. So, we (the display
  // driver) must enforce the eDP T12 power delay. In other words, we must make
  // sure that that the delay between setting `force` to false and setting it
  // back to true is at least T12. Additional documentation sources:
  // * Kaby Lake - IHD-OS-KBL-Vol 16-1.17 page 20
  // * Skyake - IHD-OS-SKL-Vol 16-05.16 page 9
  //
  // The Intel documentation references the T4 delay from the SPWG Notebook
  // Panel Specification 3.8, Section 5.9 "Panel Power Sequence", page 26. The
  // T4 delay there is equivalent to the T12 delay in the eDP Standard version
  // 1.4b (revised on December 31, 2020), Section 11 "Power Sequencing", pages
  // 249 and 251.
  DEF_BIT(3, vdd_always_on);

  // If true, the backlight is on when the panel is in the powered on state.
  DEF_BIT(2, backlight_enabled);

  // If true, panel runs power down sequence when reset is detected.
  //
  // Recommended for preserving the panel's lifetime.
  DEF_BIT(1, power_down_on_reset);

  // If true, the panel will eventually be powered on. This may initiate a panel
  // power on sequence, which would require waiting for an ongoing power off
  // sequence to complete, and then honoring the T12 delay.
  //
  // If false, the panel will eventually be powered off. This may initiate a
  // power off sequence, which would require waiting for an ongoing power on
  // sequence to complete, and then honoring the TXX delay.
  //
  // The panel power on sequence must not be initiated until all panel delays
  // are set correctly.
  DEF_BIT(0, power_state_target);

  static auto Get() { return hwreg::RegisterAddr<PchPanelPowerControl>(0xc7204); }

  // Tiger Lake has another instance for a 2nd panel at 0xc7304.
};

// PP_DIVISOR (Panel Power Cycle Delay and Reference Divisor)
//
// On Tiger Lake and DG1, the T12 value is stored in PP_CONTROL, and there is no
// documented register for setting the panel clock divisor.
//
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 629
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 623
class PchPanelPowerClockDelay : public hwreg::RegisterBase<PchPanelPowerClockDelay, uint32_t> {
 public:
  // Divider that generates the panel power clock from the PCH raw clock.
  //
  // Value = divider / 2 - 1. 0 is not a valid value.
  //
  // Intel's PRMs state that the panel clock must always be 10 kHz. This results
  // in a 100us period, which is assumed to be the base unit for all panel
  // timings.
  DEF_FIELD(31, 8, clock_divider);

  // eDP T12 - Required delay from panel power disable to power enable.
  //
  // Value = (desired_delay / 100ms) + 1.
  // Zero means no delay, and also stops a current delay.
  //
  // This field is stored in PP_CONTROL on DG1.
  DEF_FIELD(4, 0, power_cycle_delay);

  static auto Get() { return hwreg::RegisterAddr<PchPanelPowerClockDelay>(0xc7210); }
};

// PP_OFF_DELAYS (Panel Power Off Sequencing Delays)
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 963
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 988
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 629
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 623
class PchPanelPowerOffDelays : public hwreg::RegisterBase<PchPanelPowerOffDelays, uint32_t> {
 public:
  DEF_RSVDZ_FIELD(31, 29);

  // eDP T10 - Minimum delay from valid video data end to panel power disabled.
  // eDP T10 = register value * 100us.
  DEF_FIELD(28, 16, video_end_to_power_off_delay);

  DEF_RSVDZ_FIELD(15, 13);

  // eDP T9 - Minimum delay from backlight disabled to valid video data end.
  // eDP T9 = register value * 100us.
  DEF_FIELD(12, 0, backlight_off_to_video_end_delay);

  static auto Get() { return hwreg::RegisterAddr<PchPanelPowerOffDelays>(0xc720c); }

  // Tiger Lake has another instance for a 2nd panel at 0xc730c.
};

// PP_ON_DELAYS (Panel Power On Sequencing Delays)
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 964
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 989
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 630
// Skylake:  IHD-OS-SKL-Vol 2c-05.16 Part 2 page 624
class PchPanelPowerOnDelays : public hwreg::RegisterBase<PchPanelPowerOnDelays, uint32_t> {
 public:
  DEF_RSVDZ_FIELD(31, 29);

  // eDP T3 - Expected delay from enabling panel power to HPD and AUX ready.
  // eDP T3 = register value * 100us.
  DEF_FIELD(28, 16, power_on_to_hpd_aux_ready_delay);

  DEF_RSVDZ_FIELD(15, 13);

  // Minimum delay from power on until the backlight can be enabled.
  // The PCH will not enable the backlight until T3 and this delay have passed.
  // Delay = register value * 100us.
  //
  // This seems to match eDP T2 - the minimum delay from enabling panel
  // power to Automatic Black Video Generation, where the panel renders black
  // video instead of noise when it gets an invalid video signal.
  DEF_FIELD(12, 0, power_on_to_backlight_on_delay);

  static auto Get() { return hwreg::RegisterAddr<PchPanelPowerOnDelays>(0xc7208); }

  // Tiger Lake has another instance for a 2nd panel at 0xc7308.
};

// PP_STATUS (Panel Power Status)
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 965-966
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 990
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 pages 631-632
// Skylake:  IHD-OS-SKL-Vol 2c-05.16 Part 2 page 625
class PchPanelPowerStatus : public hwreg::RegisterBase<PchPanelPowerStatus, uint32_t> {
 public:
  enum class Transition : int {
    kNone = 0,          // Not in a power sequence.
    kPoweringUp = 1,    // Powering up, or waiting for T12 delay.
    kPoweringDown = 2,  // Powering down.
    kInvalid = 3,       // Reserved value.
  };

  // If true, the panel is powered up. It may be powering down.
  // If false, the panel is powered down. A T12 delay may be in effect.
  DEF_BIT(31, panel_on);

  DEF_RSVDZ_BIT(30);

  DEF_FIELD(29, 28, power_transition_bits);
  Transition PowerTransition() const { return static_cast<Transition>(power_transition_bits()); }

  // If true, a T12 delay (power down to power up) is in effect.
  DEF_BIT(27, power_cycle_delay_active);

  DEF_RSVDZ_FIELD(26, 4);

  static auto Get() { return hwreg::RegisterAddr<PchPanelPowerStatus>(0xc7200); }

  // Tiger Lake has another instance for a 2nd panel at 0xc7300.
};

}  // namespace tgl_registers

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_H_
