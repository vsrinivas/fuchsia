// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_H_

#include <zircon/assert.h>

#include <hwreg/bitfields.h>

#include "src/graphics/display/drivers/intel-i915-tgl/registers-ddi.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-dpll.h"

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

// DISPLAY_INT_CTL (Display Interrupt Control)
//
// Controls whether display interrupts propagate to the PCI device interrupt,
// and summarizes the pending display interrupts.
//
// This register is referred to as MASTER_INT_CTL (Master Interrupt Control) in
// the documentation for Kaby Lake and Skylake.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 1 pages 1054-1055
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 pages 9-11
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 pages 10-12
class DisplayInterruptControl : public hwreg::RegisterBase<DisplayInterruptControl, uint32_t> {
 public:
  // If true, display engine interrupts propagate to the next level.
  //
  // On Tiger Lake and DG1, display engine interrupts propagate to the graphics
  // interrupt dispatcher, which is controlled by the GraphicsPrimaryInterrupt
  // register.
  //
  // On Kaby Lake and Skylake, display engine interrupts propagate directly to
  // the PCI device interrupt.
  //
  // The driver sets this bit when it is ready to process display interrupts.
  DEF_BIT(31, interrupts_enabled);

  // True if there are pending interrupts from the PCU (Power Control Unit).
  //
  // This is not documented on Tiger Lake. However, it has good read semantics.
  // The underlying bit is reserved and documented as MBZ (must be zero), so
  // reading it will always report that no interrupts are pending.
  DEF_BIT(30, pcu_pending_kaby_lake);

  // True if there are pending interrupts from the audio codec.
  DEF_BIT(24, audio_codec_pending);

  // True if there are pending interrupts from the PCH display engine.
  //
  // If this bit is set, the {{PCH interrupts register}} must be checked.
  DEF_BIT(23, pch_engine_pending);

  // True if there are pending miscellaneous display engine interrupts.
  DEF_BIT(22, misc_display_pending);

  // True if there are pending hotplug interrupts from the main display engine.
  //
  // This field only reflects hotplug events from the main / north display
  // engine. Hotplug interrupts from the PCH / south display engine are recorded
  // in the `pch_engine_pending` bit.
  //
  // This bit is not documented on Kaby Lake and Skylake, where the display
  // engine does not handle hot plug for any port.
  DEF_BIT(21, display_hot_plug_pending_tiger_lake);

  // True if there are pending port interrupts.
  DEF_BIT(20, port_pending);

  // True if there are pending Pipe D interrupts.
  //
  // This field is not documented on Kaby Lake and Skylake. The underlying bit
  // is reserved but not documented as MBZ (Must Be Zero).
  DEF_BIT(19, pipe_d_pending_tiger_lake);

  // True if there are pending Pipe C interrupts.
  DEF_BIT(18, pipe_c_pending);

  // True if there are pending Pipe B interrupts.
  DEF_BIT(17, pipe_b_pending);

  // True if there are pending Pipe A interrupts.
  DEF_BIT(16, pipe_a_pending);

  // True if there are pending VEBox (video encoding box) interrupts.
  //
  // This is not documented on Tiger Lake. However, it has good read semantics.
  // The underlying bit is reserved and documented as MBZ (must be zero), so
  // reading it will always report that no interrupts are pending.
  DEF_BIT(6, video_encoding_box_pending_kaby_lake);

  // True if there are pending GTPM (GPU power management) interrupts.
  //
  // This is not documented on Tiger Lake. However, it has good read semantics.
  // The underlying bit is reserved and documented as MBZ (must be zero), so
  // reading it will always report that no interrupts are pending.
  DEF_BIT(4, gpu_power_pending_kaby_lake);

  // True if there are pending VCS (Video Command Streamer) 2 interrupts.
  //
  // This is not documented on Tiger Lake. However, it has good read semantics.
  // The underlying bit is reserved and documented as MBZ (must be zero), so
  // reading it will always report that no interrupts are pending.
  DEF_BIT(3, video_command_streamer_2_pending_kaby_lake);

  // True if there are pending VCS (Video Command Streamer) 1 interrupts.
  //
  // This is not documented on Tiger Lake. However, it has good read semantics.
  // The underlying bit is reserved and documented as MBZ (must be zero), so
  // reading it will always report that no interrupts are pending.
  DEF_BIT(2, video_command_streamer_1_pending_kaby_lake);

  // True if there are pending blitter interrupts.
  //
  // This is not documented on Tiger Lake. However, it has good read semantics.
  // The underlying bit is reserved and documented as MBZ (must be zero), so
  // reading it will always report that no interrupts are pending.
  DEF_BIT(1, blitter_pending_kaby_lake);

  // True if there are pending render interrupts.
  //
  // This is not documented on Tiger Lake. However, it has good read semantics.
  // The underlying bit is reserved and documented as MBZ (must be zero), so
  // reading it will always report that no interrupts are pending.
  DEF_BIT(0, render_pending_kaby_lake);

  static auto Get() { return hwreg::RegisterAddr<DisplayInterruptControl>(0x44200); }
};

// GFX_MSTR_INTR (Graphics Primary Interrupt)
//
// Controls whether top-level graphics interrupts propagate to the PCI device
// interrupt, and summarizes the pending graphics-level interrupts.
//
// This register is not documented on Kaby Lake or Skylake. On those platforms,
// the display engine interrupts covered by DisplayInterruptControl propagate
// directly to the PCI device interrupt.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 1 pages 1054-1055
class GraphicsPrimaryInterrupt : public hwreg::RegisterBase<GraphicsPrimaryInterrupt, uint32_t> {
 public:
  // If true, graphics interrupts propagate to the PCI device interrupt.
  //
  // The driver sets this bit when it is ready to process graphics interrupts.
  DEF_BIT(31, interrupts_enabled);

  // True if an interrupt from the display engine is pending.
  DEF_BIT(16, display_interrupt_pending);

  // True if a GPU interrupt is pending.
  DEF_BIT(1, gt1_interrupt_pending);

  // True if a GPU interrupt is pending.
  DEF_BIT(0, gt0_interrupt_pending);

  static auto Get() { return hwreg::RegisterAddr<GraphicsPrimaryInterrupt>(0x190010); }
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

  hwreg::BitfieldRef<uint32_t> ddi_io_power_request_skylake(i915_tgl::DdiId ddi_id) {
    int bit =
        2 +
        ((ddi_id == i915_tgl::DdiId::DDI_A || ddi_id == i915_tgl::DdiId::DDI_E) ? 0 : ddi_id * 2) +
        1;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> ddi_io_power_state_skylake(i915_tgl::DdiId ddi_id) {
    int bit =
        2 +
        ((ddi_id == i915_tgl::DdiId::DDI_A || ddi_id == i915_tgl::DdiId::DDI_E) ? 0 : ddi_id * 2);
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

  SelfType& set_power_on_request_combo_or_usb_c(i915_tgl::DdiId ddi_id, bool enabled) {
    ZX_DEBUG_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_A);
    ZX_DEBUG_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_TC_6);
    const uint32_t bit = ddi_id * 2 + 1;
    hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit).set(enabled);
    return *this;
  }

  bool powered_on_combo_or_usb_c(i915_tgl::DdiId ddi_id) const {
    ZX_DEBUG_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_A);
    ZX_DEBUG_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_TC_6);
    const uint32_t bit = ddi_id * 2;
    return hwreg::BitfieldRef<const uint32_t>(reg_value_ptr(), bit, bit).get();
  }

  SelfType& set_power_on_request_thunderbolt(i915_tgl::DdiId ddi_id, bool enabled) {
    ZX_DEBUG_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_TC_1);
    ZX_DEBUG_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_TC_6);
    // The request_thunderbolt_* bits start from bit 19.
    const uint32_t bit = (ddi_id - i915_tgl::DdiId::DDI_TC_1) * 2 + 19;
    hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit).set(enabled);
    return *this;
  }

  bool powered_on_thunderbolt(i915_tgl::DdiId ddi_id) const {
    ZX_DEBUG_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_TC_1);
    ZX_DEBUG_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_TC_6);
    // The state_thunderbolt_* bits start from bit 18.
    const uint32_t bit = (ddi_id - i915_tgl::DdiId::DDI_TC_1) * 2 + 18;
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
  hwreg::BitfieldRef<uint32_t> ddi_io_power_request_tiger_lake(i915_tgl::DdiId ddi_id) {
    // DDI A-C: bits 1/3/5. DDI TC1-6: bits 7/9/11/13/15/17.
    int bit = ddi_id * 2 + 1;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> ddi_io_power_state_tiger_lake(i915_tgl::DdiId ddi_id) {
    int bit = ddi_id * 2;  // DDI A-C: bits 0/2/4. DDI TC1-6: bits 6/8/10/12/14/16.
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

// DBUF_CTL (DBUF Slice Control)
//
// Some of the register's reserved are not documented as MBZ (must be zero). So,
// this register can only be safely updated using read-modify-write operations.
//
// The "Data Buffer" expansion for the DBUF acronym is implicitly documented in
// the "Shared Functions" > "Data Buffer" section of the display engine PRMs
// (Vol 12 for all recent display engines), which lists all the DBUF-related
// registers.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-12.21 Part 1 pages 331-332
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 1 pages 309-310
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 pages 430-431
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 pages 426-427
class DataBufferControl : public hwreg::RegisterBase<DataBufferControl, uint32_t> {
 public:
  // The eventual state that `powered_on` will reach.
  //
  // If true, the DBUF (Data Buffer) slice will eventually power on. If false,
  // the DBUF slice will eventually power off.
  //
  // The first DBUF slice must be powered on before using the display engine.
  // Powering it on is a step in the "Sequences to Initialize Display".
  DEF_BIT(31, powered_on_target);

  // If true, the DBUF (Data Buffer) slice is powered on.
  DEF_BIT(30, powered_on);

  // Maximum number of clock cycles until the tracker state is serviced.
  //
  // This field is not documented on Kaby Lake and Skylake. The underlying bits
  // are documented as reserved MBZ (must be zero).
  DEF_FIELD(23, 19, maximum_tracker_state_delay_tiger_lake);

  // Maximum number of clock cycles until the CC block valid state is serviced.
  //
  // This field is not documented on Kaby Lake and Skylake. The underlying bits
  // are documented as reserved MBZ (must be zero).
  DEF_FIELD(15, 12, maximum_cc_block_valid_delay);

  // DBUF (Data Buffer) slice count varies by display engine.
  //
  // `slice_index` is 0-based.
  static auto GetForSlice(int slice_index) {
    ZX_ASSERT(slice_index >= 0);
    ZX_ASSERT(slice_index < 2);

    static constexpr uint32_t kMmioAddress[] = {0x45008, 0x44fe8};
    return hwreg::RegisterAddr<DataBufferControl>(kMmioAddress[slice_index]);
  }
};

// DBUF_CTL (DBUF Slice Control 2)
//
// All reserved bits are MBZ (must be zero), so this register can be written
// safely without reading it first.
//
// This register is not documented on Kaby Lake or Skylake.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-12.21 Part 1 page 333
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 1 page 311
class DataBufferControl2 : public hwreg::RegisterBase<DataBufferControl2, uint32_t> {
 public:
  DEF_RSVDZ_FIELD(31, 13);

  // Number of time slots between servicing HPUTs.
  //
  // This field must not be set to zero.
  DEF_FIELD(12, 8, hput_service_interval);

  // Number of time slots dedicated to servicing APUTs.
  //
  // This field must not be set to zero.
  DEF_FIELD(7, 4, aput_service_time_slots);

  // Number of time slots dedicated to servicing BYPASS puts.
  //
  // This field must not be set to zero.
  DEF_FIELD(3, 0, bypass_put_service_time_slots);

  // DBUF (Data Buffer) slice count varies by display engine.
  //
  // `slice_index` is 0-based.
  static auto GetForSlice(int slice_index) {
    ZX_ASSERT(slice_index >= 0);
    ZX_ASSERT(slice_index < 2);

    static constexpr uint32_t kMmioAddress[] = {0x44ffc, 0x44fe4};
    return hwreg::RegisterAddr<DataBufferControl2>(kMmioAddress[slice_index]);
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

  static auto Get(i915_tgl::DdiId ddi_id) {
    if (ddi_id == i915_tgl::DdiId::DDI_B) {
      return hwreg::RegisterAddr<GpioCtl>(0xc5020);
    }
    if (ddi_id == i915_tgl::DdiId::DDI_C) {
      return hwreg::RegisterAddr<GpioCtl>(0xc501c);
    }
    ZX_DEBUG_ASSERT(ddi_id == i915_tgl::DdiId::DDI_D);
    return hwreg::RegisterAddr<GpioCtl>(0xc5024);
  }
};

}  // namespace tgl_registers

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_H_
