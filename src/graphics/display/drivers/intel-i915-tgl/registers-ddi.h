// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_DDI_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_DDI_H_

#include <lib/ddk/debug.h>
#include <zircon/assert.h>

#include <cstdint>
#include <optional>

#include <hwreg/bitfields.h>

#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"

namespace tgl_registers {

// Interrupt registers for the south (in the PCH) display engine.
//
// SINTERRUPT is made up of the interrupt registers below.
// - ISR (Interrupt Status Register), also abbreviated to SDE_ISR
// - IMR (Interrupt Mask Register), also abbreviated to SDE_IMR
// - IIR (Interrupt Identity Register), also abbreviated to SDE_IIR
// - IER (Interrupt Enable Register), also abbreviated to SDE_IER
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 1196-1197
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 pages 820-821
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 pages 800-801
//
// The individual bits in each register are covered in the South Display Engine
// Interrupt Bit Definition, or SDE_INTERRUPT.
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 1262-1264
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 1328-1329
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 pages 874-875
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 pages 854-855
class SdeInterruptBase : public hwreg::RegisterBase<SdeInterruptBase, uint32_t> {
 public:
  // SDE_INTERRUPT documents the base MMIO offset. SINTERRUPT documents the
  // individual register offsets.
  static constexpr uint32_t kSdeIntMask = 0xc4004;
  static constexpr uint32_t kSdeIntIdentity = 0xc4008;
  static constexpr uint32_t kSdeIntEnable = 0xc400c;

  hwreg::BitfieldRef<uint32_t> skl_ddi_bit(i915_tgl::DdiId ddi_id) {
    uint32_t bit;
    switch (ddi_id) {
      case i915_tgl::DdiId::DDI_A:
        bit = 24;
        break;
      case i915_tgl::DdiId::DDI_B:
      case i915_tgl::DdiId::DDI_C:
      case i915_tgl::DdiId::DDI_D:
        bit = 20 + ddi_id;
        break;
      case i915_tgl::DdiId::DDI_E:
        bit = 25;
        break;
      default:
        bit = -1;
    }
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> icl_ddi_bit(i915_tgl::DdiId ddi_id) {
    uint32_t bit;
    switch (ddi_id) {
      case i915_tgl::DdiId::DDI_A:
      case i915_tgl::DdiId::DDI_B:
      case i915_tgl::DdiId::DDI_C:
        bit = 16 + ddi_id - i915_tgl::DdiId::DDI_A;
        break;
      case i915_tgl::DdiId::DDI_TC_1:
      case i915_tgl::DdiId::DDI_TC_2:
      case i915_tgl::DdiId::DDI_TC_3:
      case i915_tgl::DdiId::DDI_TC_4:
      case i915_tgl::DdiId::DDI_TC_5:
      case i915_tgl::DdiId::DDI_TC_6:
        bit = 24 + ddi_id - i915_tgl::DdiId::DDI_TC_1;
        break;
      default:
        bit = -1;
    }
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  static auto Get(uint32_t offset) { return hwreg::RegisterAddr<SdeInterruptBase>(offset); }
};

// DE_HPD_INTERRUPT : Display Engine HPD Interrupts for Type C / Thunderbolt (since gen11)
class HpdInterruptBase : public hwreg::RegisterBase<HpdInterruptBase, uint32_t> {
 public:
  static constexpr uint32_t kHpdIntMask = 0x44474;
  static constexpr uint32_t kHpdIntIdentity = 0x44478;
  static constexpr uint32_t kHpdIntEnable = 0x4447c;

  hwreg::BitfieldRef<uint32_t> tc_hotplug(i915_tgl::DdiId ddi_id) {
    ZX_DEBUG_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_TC_1);
    ZX_DEBUG_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_TC_6);
    uint32_t bit = 16 + ddi_id - i915_tgl::DdiId::DDI_TC_1;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> tbt_hotplug(i915_tgl::DdiId ddi_id) {
    ZX_DEBUG_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_TC_1);
    ZX_DEBUG_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_TC_6);
    uint32_t bit = ddi_id - i915_tgl::DdiId::DDI_TC_1;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  static auto Get(uint32_t offset) { return hwreg::RegisterAddr<HpdInterruptBase>(offset); }
};

// TBT_HOTPLUG_CTL : Thunderbolt Hot Plug Control (since gen11)
class TbtHotplugCtrl : public hwreg::RegisterBase<TbtHotplugCtrl, uint32_t> {
 public:
  hwreg::BitfieldRef<uint32_t> hpd_enable(i915_tgl::DdiId ddi_id) {
    uint32_t bit = ddi_id_to_first_bit(ddi_id) + kHpdEnableBitSubOffset;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> hpd_long_pulse(i915_tgl::DdiId ddi_id) {
    uint32_t bit = ddi_id_to_first_bit(ddi_id) + kHpdLongPulseBitSubOffset;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> hpd_short_pulse(i915_tgl::DdiId ddi_id) {
    uint32_t bit = ddi_id_to_first_bit(ddi_id) + kHpdShortPulseBitSubOffset;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  static auto Get() { return hwreg::RegisterAddr<TbtHotplugCtrl>(kOffset); }

 private:
  static constexpr uint32_t kOffset = 0x44030;

  static constexpr uint32_t kHpdShortPulseBitSubOffset = 0;
  static constexpr uint32_t kHpdLongPulseBitSubOffset = 1;
  static constexpr uint32_t kHpdEnableBitSubOffset = 3;

  static uint32_t ddi_id_to_first_bit(i915_tgl::DdiId ddi_id) {
    switch (ddi_id) {
      case i915_tgl::DdiId::DDI_A:
      case i915_tgl::DdiId::DDI_B:
      case i915_tgl::DdiId::DDI_C:
        ZX_DEBUG_ASSERT_MSG(false, "Use south display hot plug registers for DDI A-C");
        return -1;
      case i915_tgl::DdiId::DDI_TC_1:
      case i915_tgl::DdiId::DDI_TC_2:
      case i915_tgl::DdiId::DDI_TC_3:
      case i915_tgl::DdiId::DDI_TC_4:
      case i915_tgl::DdiId::DDI_TC_5:
      case i915_tgl::DdiId::DDI_TC_6:
        return 4 * (ddi_id - i915_tgl::DdiId::DDI_TC_1);
    }
  }
};

// TC_HOTPLUG_CTL : Type-C Hot Plug Control (since gen11)
class TcHotplugCtrl : public hwreg::RegisterBase<TcHotplugCtrl, uint32_t> {
 public:
  hwreg::BitfieldRef<uint32_t> hpd_enable(i915_tgl::DdiId ddi_id) {
    uint32_t bit = ddi_id_to_first_bit(ddi_id) + kHpdEnableBitSubOffset;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> hpd_long_pulse(i915_tgl::DdiId ddi_id) {
    uint32_t bit = ddi_id_to_first_bit(ddi_id) + kHpdLongPulseBitSubOffset;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> hpd_short_pulse(i915_tgl::DdiId ddi_id) {
    uint32_t bit = ddi_id_to_first_bit(ddi_id) + kHpdShortPulseBitSubOffset;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  static auto Get() { return hwreg::RegisterAddr<TcHotplugCtrl>(kOffset); }

 private:
  static constexpr uint32_t kOffset = 0x44038;

  static constexpr uint32_t kHpdShortPulseBitSubOffset = 0;
  static constexpr uint32_t kHpdLongPulseBitSubOffset = 1;
  static constexpr uint32_t kHpdEnableBitSubOffset = 3;

  static uint32_t ddi_id_to_first_bit(i915_tgl::DdiId ddi_id) {
    switch (ddi_id) {
      case i915_tgl::DdiId::DDI_A:
      case i915_tgl::DdiId::DDI_B:
      case i915_tgl::DdiId::DDI_C:
        ZX_DEBUG_ASSERT_MSG(false, "Use south display hot plug registers for DDI A-C");
        return -1;
      case i915_tgl::DdiId::DDI_TC_1:
      case i915_tgl::DdiId::DDI_TC_2:
      case i915_tgl::DdiId::DDI_TC_3:
      case i915_tgl::DdiId::DDI_TC_4:
      case i915_tgl::DdiId::DDI_TC_5:
      case i915_tgl::DdiId::DDI_TC_6:
        return 4 * (ddi_id - i915_tgl::DdiId::DDI_TC_1);
    }
  }
};

// SHOTPLUG_CTL_DDI + SHOTPLUG_CTL_TC
class IclSouthHotplugCtrl : public hwreg::RegisterBase<IclSouthHotplugCtrl, uint32_t> {
 public:
  hwreg::BitfieldRef<uint32_t> hpd_enable(i915_tgl::DdiId ddi_id) {
    uint32_t bit = ddi_id_to_first_bit(ddi_id) + kHpdEnableBitSubOffset;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> hpd_long_pulse(i915_tgl::DdiId ddi_id) {
    uint32_t bit = ddi_id_to_first_bit(ddi_id) + kHpdLongPulseBitSubOffset;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> hpd_short_pulse(i915_tgl::DdiId ddi_id) {
    uint32_t bit = ddi_id_to_first_bit(ddi_id) + kHpdShortPulseBitSubOffset;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  static auto Get(i915_tgl::DdiId ddi_id) {
    return hwreg::RegisterAddr<IclSouthHotplugCtrl>(
        ddi_id >= i915_tgl::DdiId::DDI_TC_1 ? kTcOffset : kDdiOffset);
  }

 private:
  static constexpr uint32_t kDdiOffset = 0xc4030;
  static constexpr uint32_t kTcOffset = 0xc4034;

  static constexpr uint32_t kHpdShortPulseBitSubOffset = 0;
  static constexpr uint32_t kHpdLongPulseBitSubOffset = 1;
  static constexpr uint32_t kHpdEnableBitSubOffset = 3;

  static uint32_t ddi_id_to_first_bit(i915_tgl::DdiId ddi_id) {
    switch (ddi_id) {
      case i915_tgl::DdiId::DDI_A:
      case i915_tgl::DdiId::DDI_B:
      case i915_tgl::DdiId::DDI_C:
        return 4 * (ddi_id - i915_tgl::DdiId::DDI_A);  // SHOTPLUG_CTL_DDI
      case i915_tgl::DdiId::DDI_TC_1:
      case i915_tgl::DdiId::DDI_TC_2:
      case i915_tgl::DdiId::DDI_TC_3:
      case i915_tgl::DdiId::DDI_TC_4:
      case i915_tgl::DdiId::DDI_TC_5:
      case i915_tgl::DdiId::DDI_TC_6:
        return 4 * (ddi_id - i915_tgl::DdiId::DDI_TC_1);  // SHOTPLUG_CTL_TC
      default:
        return -1;
    }
  }
};

// SHOTPLUG_CTL + SHOTPLUG_CTL2
class SouthHotplugCtrl : public hwreg::RegisterBase<SouthHotplugCtrl, uint32_t> {
 public:
  hwreg::BitfieldRef<uint32_t> hpd_enable(i915_tgl::DdiId ddi_id) {
    uint32_t bit = ddi_id_to_first_bit(ddi_id) + kHpdEnableBitSubOffset;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> hpd_long_pulse(i915_tgl::DdiId ddi_id) {
    uint32_t bit = ddi_id_to_first_bit(ddi_id) + kHpdLongPulseBitSubOffset;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  hwreg::BitfieldRef<uint32_t> hpd_short_pulse(i915_tgl::DdiId ddi_id) {
    uint32_t bit = ddi_id_to_first_bit(ddi_id) + kHpdShortPulseBitSubOffset;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit, bit);
  }

  static auto Get(i915_tgl::DdiId ddi_id) {
    return hwreg::RegisterAddr<SouthHotplugCtrl>(ddi_id == i915_tgl::DdiId::DDI_E ? kOffset2
                                                                                  : kOffset);
  }

 private:
  static constexpr uint32_t kOffset = 0xc4030;
  static constexpr uint32_t kOffset2 = 0xc403c;

  static constexpr uint32_t kHpdShortPulseBitSubOffset = 0;
  static constexpr uint32_t kHpdLongPulseBitSubOffset = 1;
  static constexpr uint32_t kHpdEnableBitSubOffset = 4;

  static uint32_t ddi_id_to_first_bit(i915_tgl::DdiId ddi_id) {
    switch (ddi_id) {
      case i915_tgl::DdiId::DDI_A:
        return 24;
      case i915_tgl::DdiId::DDI_B:
      case i915_tgl::DdiId::DDI_C:
      case i915_tgl::DdiId::DDI_D:
        return 8 * (ddi_id - 1);
      case i915_tgl::DdiId::DDI_E:
        return 0;
      default:
        return -1;
    }
  }
};

// SFUSE_STRAP (South / PCH Fuses and Straps)
//
// This register is not documented on DG1.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 1185
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 811
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 791
class PchDisplayFuses : public hwreg::RegisterBase<PchDisplayFuses, uint32_t> {
 public:
  // On Tiger Lake, indicates whether RawClk should be clocked at 24MHz or
  // 19.2MHz.
  DEF_BIT(8, rawclk_is_24mhz);

  // Not present (set to zero) on Tiger Lake. The driver is expected to use the
  // VBT (Video BIOS Table) or hotplug detection to figure out which ports are
  // present.
  DEF_BIT(2, port_b_present);
  DEF_BIT(1, port_c_present);
  DEF_BIT(0, port_d_present);

  static auto Get() { return hwreg::RegisterAddr<PchDisplayFuses>(0xc2014); }
};

// DDI_BUF_CTL (DDI Buffer Control)
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 1 pages 352-355
// DG1: IHD-OS-DG1-Vol 2c-2.21 pages 331-334
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 pages 442-445
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 pages 438-441
class DdiBufferControl : public hwreg::RegisterBase<DdiBufferControl, uint32_t> {
 public:
  // If true, the DDI buffer is enabled.
  DEF_BIT(31, enabled);

  // If true, the DDI ignores PHY parameter changes during link training.
  //
  // The impacted PHY parameters include voltage swing and pre-emphasis. This
  // field is generally set when using specific PHY parameters for the DDI.
  //
  // This field does not exist (is reserved) on Kaby Lake and Skylake.
  DEF_BIT(29, override_training_tiger_lake);

  // If true, the DDI uses adjusted PHY parameter values.
  //
  // The value is ignored if `override_training` is false.
  //
  // This field does not exist (is reserved) on Kaby Lake and Skylake.
  DEF_BIT(28, adjust_phy_parameters_tiger_lake);

  // Selects one of the DisplayPort PHY configurations set up in DDI_BUF_TRANS.
  //
  // DDIs A and E support indexes 0 through 9. DDIs B-D only support indexes 0
  // through 8, because the 9th PHY configuration is used for HDMI.
  //
  // This field is meaningless for HDMI and DVI.
  //
  // This field does not exist (is reserved) on Tiger Lake and DG1.
  DEF_FIELD(27, 24, display_port_phy_config_kaby_lake);

  // If true, data is swapped on the lanes output by the port.
  //
  // This field must not be changed while the DDI is enabled.
  //
  // Tiger Lake and DG1:
  //
  // FIA handles lane reversal for Thunderbolt and USB-C DisplayPort Alt Mode,
  // and this field should be set to false in those cases. Static and fixed
  // connections (DisplayPort, HDMI) through the FIA only use this field in
  // "No pin Assignment (Non Type-C DP)" static configurations. Other
  // connections use the field.
  //
  // Kaby Lake and Skylake:
  //
  // For DDIs B-D, enabling swaps lanes 0 <-> 3 and lanes 1 <-> 2. If DDI E is
  // enabled (in DDI A Lane Capability Control), then DDI A reversal swaps its
  // two remaining lanes (0 <-> 1). If DDI E is disabled, DDI A reversal acts
  // the same as B-D reversal (lanes 0 <-> 3 and 1 <->2 are swapped). DDI E does
  // not support port reversal.
  DEF_BIT(16, port_reversal);

  // Delay used to stagger the assertion/deassertion of the port lane enables.
  //
  // The value is expressed in multiples of the symbol clock period, so it
  // depends on the link frequency.
  //
  // The delay should be at least 100ns when the port is used in USB Type C
  // mode. In all other cases, the delay should be zero.
  //
  // This field does not exist (is reserved) on Kaby Lake and Skylake, which
  // don't have Type C DDIs.
  DEF_FIELD(15, 8, type_c_display_port_lane_staggering_delay_tiger_lake);

  // If true, the DDI is idle.
  DEF_BIT(7, is_idle);

  // If false, two lanes from DDI A are repurposed to form DDI E.
  //
  // If true, DDI A has four lanes, and behaves similarly to DDIs B-D. If false,
  //
  // This field is only meaningful on DDI A, whose lanes get redistributed to
  // DDI E. The field must be programmed at boot time (based on the board
  // configuration) and must not be changed afterwards.
  //
  // This field does not exist (is reserved) on Tiger Lake or DG1.
  DEF_BIT(4, ddi_e_disabled_kaby_lake);

  // Selects the number of DisplayPort lanes enabled.
  //
  // The field's value is the number of lanes minus 1. 0 = x1 lane, 1 = x2
  // lanes, 3 = x4 lanes. display_port_lane_count() and
  // set_display_port_lane_count() take care of this encoding detail.
  DEF_FIELD(3, 1, display_port_lane_count_selection);

  // The number of DisplayPort lanes enabled.
  //
  // This field is not meaningful for HDMI, which always uses all the lanes.
  //
  // When the DDI is in DisplayPort mode, the field must match the corresponding
  // setting in TRANS_DDI_FUNC_CTL for the transcoder attached to this DDI.
  //
  // On Kaby Lake and Skylake, DDI E only supports 1 and 2 lanes
  // (if it's enabled), since it takes two lanes from DDI A. On the same
  // hardware, DDI A always supports x1 and x2, and supports x4 if DDI E is
  // disabled (and therefore not taking away 2 lanes from DDI A).
  uint8_t display_port_lane_count() const {
    // The addition will not overflow and the cast is lossless because
    // display_port_lane_count_selection() is a 3-bit field.
    return static_cast<int8_t>(display_port_lane_count_selection() + 1);
  }

  // See display_port_lane_count() for details.
  DdiBufferControl& set_display_port_lane_count(uint8_t lane_count) {
    switch (lane_count) {
      case 1:
      case 2:
      case 4:
        set_display_port_lane_count_selection(lane_count - 1);
        return *this;
    }
    ZX_ASSERT_MSG(false, "Unsupported lane count: %d", lane_count);
    return *this;
  }

  // The level of the port detect pin at boot time.
  //
  // This field is only meaningful on DDI A. On Skylake and Kaby Lake, the other
  // DDIs' port detect pin status is communicated in SFUSE_STRAP.
  DEF_BIT(0, boot_time_port_detect_pin_status);

  // For Kaby Lake and Skylake DDI A - DDI E.
  static auto GetForKabyLakeDdi(i915_tgl::DdiId ddi_id) {
    ZX_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_A);
    ZX_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_E);

    const int ddi_index = ddi_id - i915_tgl::DdiId::DDI_A;
    return hwreg::RegisterAddr<DdiBufferControl>(0x64000 + 0x100 * ddi_index);
  }

  // For Tiger Lake and DG1.
  static auto GetForTigerLakeDdi(i915_tgl::DdiId ddi_id) {
    ZX_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_A);
    ZX_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_TC_6);

    const int ddi_index = ddi_id - i915_tgl::DdiId::DDI_A;
    return hwreg::RegisterAddr<DdiBufferControl>(0x64000 + 0x100 * ddi_index);
  }
};

// Part 1 of DDI_BUF_TRANS (DDI Buffer Translation)
//
// Each DDI has 10 instances of the DDI_BUF_TRANS register, storing 10 entries
// of the port's PHY configuration table. The MMIO addresses for the 10
// instances are consecutive. The active entry is selected using the DDI_BUF_CTL
// register.
//
// Each DDI_BUF_TRANS register instance (storing one entry in the PHY
// configuration table) consists of two 32-bit parts (double-words). We don't
// know if it's safe to use 64-bit MMIO accesses with the registers.
//
// DDI_BUF_TRANS is not documented on Tiger Lake or DG1.
//
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 pages 446-447
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 pages 442-441
class DdiPhyConfigEntry1 : public hwreg::RegisterBase<DdiPhyConfigEntry1, uint32_t> {
 public:
  // The PRMs do not go in depth on the meanings of the fields.
  DEF_BIT(31, balance_leg_enable);
  DEF_FIELD(17, 0, deemphasis_level);

  static auto GetDdiInstance(i915_tgl::DdiId ddi_id, int instance_index) {
    ZX_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_A);
    ZX_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_E);
    ZX_ASSERT(instance_index >= 0);
    ZX_ASSERT(instance_index < 10);

    const int ddi_index = ddi_id - i915_tgl::DdiId::DDI_A;
    return hwreg::RegisterAddr<DdiPhyConfigEntry1>(0x64e00 + 0x60 * ddi_index + 8 * instance_index);
  }
};

// Part 2 of DDI_BUF_TRANS (DDI Buffer Translation)
//
// See Part 1 above for documentation.
class DdiPhyConfigEntry2 : public hwreg::RegisterBase<DdiPhyConfigEntry2, uint32_t> {
 public:
  // The PRMs do not go in depth on the meanings of the fields.
  DEF_FIELD(20, 16, voltage_reference_select);
  DEF_FIELD(10, 0, voltage_swing);

  static auto GetDdiInstance(i915_tgl::DdiId ddi_id, int instance_index) {
    ZX_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_A);
    ZX_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_E);
    ZX_ASSERT(instance_index >= 0);
    ZX_ASSERT(instance_index < 10);

    const int ddi_index = ddi_id - i915_tgl::DdiId::DDI_A;
    return hwreg::RegisterAddr<DdiPhyConfigEntry2>(0x64e04 + 0x60 * ddi_index + 8 * instance_index);
  }
};

// DISPIO_CR_TX_BMU_CR0
//
// Involved in PHY parameters for transmission on all DDIs.
//
// This register does not exist on Tiger Lake or DG1.
//
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 pages 446-447
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 pages 442-441
class DdiPhyBalanceControl : public hwreg::RegisterBase<DdiPhyBalanceControl, uint32_t> {
 public:
  // Not managed by driver software.
  DEF_FIELD(31, 29, digital_analog);

  // Not managed by driver software.
  DEF_BIT(28, global_vs_local_voltage_reference_select);

  // Must be zero for `balance_leg_select` fields to be used.
  DEF_FIELD(27, 23, disable_balance_leg);

  // For DDI4 - DDI E or DDI A when DDI E is disabled.
  DEF_FIELD(22, 20, balance_leg_select_ddi_e);

  // For DDI3 / DDI D.
  DEF_FIELD(19, 17, balance_leg_select_ddi_d);

  // For DDI2 / DDI C.
  DEF_FIELD(16, 14, balance_leg_select_ddi_c);

  // For DDI1 / DDI B.
  DEF_FIELD(13, 11, balance_leg_select_ddi_b);

  // For DDI0 / DDI A.
  DEF_FIELD(10, 8, balance_leg_select_ddi_a);

  // Not managed by driver software.
  DEF_FIELD(7, 0, h_mode);

  hwreg::BitfieldRef<uint32_t> balance_leg_select_for_ddi(i915_tgl::DdiId ddi_id) {
    ZX_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_A);
    ZX_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_E);

    const int ddi_index = ddi_id - i915_tgl::DdiId::DDI_A;
    const int bit_index = 8 + 3 * ddi_index;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit_index + 2, bit_index);
  }

  static auto Get() { return hwreg::RegisterAddr<DdiPhyBalanceControl>(0x6c00c); }
};

// DDI_AUX_CTL (DDI AUX Channel Control)
//
// Tiger Lake: IHD-OS-TGL-Vol2c-12.21 Part 1 pages 342-345
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 1 pages 321-323
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 pages 436-438
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 pages 432-434
class DdiAuxControl : public hwreg::RegisterBase<DdiAuxControl, uint32_t> {
 public:
  // True while the DDI is performing an AUX transaction.
  //
  // The driver sets this field to true to start an AUX transaction. The
  // hardware resets it back to false when the AUX transaction is completed.
  //
  // The register should not be modified while this field is true.
  //
  // On Kaby Lake and Skylake, DDI_AUX_MUTEX must be acquired before setting up
  // an AUX transaction.
  DEF_BIT(31, transaction_in_progress);

  // Set to true by hardware when it completes an AUX transaction.
  //
  // This bit is sticky Read/Write-Clear. It stays true until the driver resets
  // it by writing true to it.
  DEF_BIT(30, transaction_done);

  // If true, an interrupt is triggered when an AUX transaction is completed.
  DEF_BIT(29, interrupt_on_done);

  // Set to true by hardware when an AUX transaction times out.
  //
  // This bit is sticky Read/Write-Clear. It stays true until the driver resets
  // it by writing true to it.
  DEF_BIT(28, timeout);

  // Selects the AUX transaction timeout.
  //
  // The AUX transaction limit in the DisplayPort specification is 500us.
  //
  // The values are documented as 0 (400us, unsupported), 1 (600us), 2 (800us)
  DEF_FIELD(27, 26, timeout_timer_select);

  // Only documented on Kaby Lake and Skylake. The docs advise against using.
  static constexpr int kTimeoutUnsupported400us = 0;

  static constexpr int kTimeout600us = 1;
  static constexpr int kTimeout800us = 2;

  // 4,000 us on Tiger Lake and DG1. 1,600 us on Kaby Lake and Skylake.
  static constexpr int kTimeoutLarge = 3;

  // Set to true by hardware when an AUX transaction receives invalid data.
  //
  // The received data could be invalid due to: corruption detected, the bits
  // received don't add up to an integer number of bytes, more than 20 bytes
  // received.
  //
  // This bit is sticky Read/Write-Clear. It stays true until the driver resets
  // it by writing true to it.
  DEF_BIT(25, receive_error);

  // Total number of bytes in an AUX message, including the message header.
  //
  // The driver writes this field to indicate the message size for the next AUX
  // transaction. The hardware writes this field to indicate the response size
  // for the last AUX transaction.
  //
  // The message includes the header bytes (4 for command, 2 for reply). The
  // DisplayPort specification states that the maximum data size is 16 bytes,
  // leading to a 20-byte maximum message size.
  //
  // The driver must write values between 1 and 20. The value read from this
  // field is only valid and meaningful if `transaction_done` is true, and
  // `transaction_in_progress`, `timeout`, and `receive_error` are false.
  DEF_FIELD(24, 20, message_size);

  // Directs AUX transactions to the Thunderbolt IO, or the USB-C / Combo IO.
  //
  // If true, transactions will be performed via the Thunderbolt controller.
  // Otherwise, the transactions will be performed over USB-C (using the FIA) or
  // over the Combo DDI IO.
  //
  // This field is reserved (must be false) on Kaby Lake and Skylake, which
  // don't support Thunderbolt IO.
  DEF_BIT(11, use_thunderbolt);

  // Number of SYNC pulses sent during SYNC for eDP fast wake transactions.
  //
  // The value is the number of SYNC pulses minus 1.
  DEF_FIELD(9, 5, fast_wake_sync_pulse_count);

  // `fast_wake_sync_pulse_count` should be set to 7, to match the Embedded
  // DisplayPort standard requirement for 8 pre-charge pulses (zeros) in the
  // AUX_PHY_WAKE preamble.
  static constexpr int kFastWakeSyncPulseCount = 8 - 1;

  // Number of SYNC pulses sent during SYNC for standard transactions.
  //
  // The value is the number of SYNC pulses minus 1. This is the sum of the
  // 10-16 pre-charge pulses (zeros) and the 16 consecutive zeros at the start
  // of the AUX_SYNC pattern.
  DEF_FIELD(4, 0, sync_pulse_count);

  // `sync_pulse_count` should be set to at least 25, to meet the DisplayPort
  // 26-pulse minimum, which is equivalent to 10 pre-charge pulses.
  static constexpr int kMinSyncPulseCount = 26 - 1;

  // For Kaby Lake and Skylake DDI A - DDI E.
  //
  // The Kaby Lake and Skylake references only document the AUX registers for
  // DDIs A-D. Other manuals, such as IHD-OS-ICLLP-Vol 2c-1.20, document AUX
  // registers for DDIs E-F, and their MMIO addresses are what we'd expect.
  // For now, we assume DDI E has an AUX channel that works like the other DDIs.
  static auto GetForKabyLakeDdi(i915_tgl::DdiId ddi_id) {
    ZX_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_A);
    ZX_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_E);

    const int ddi_index = ddi_id - i915_tgl::DdiId::DDI_A;
    return hwreg::RegisterAddr<DdiAuxControl>(0x64010 + 0x100 * ddi_index);
  }

  // For Tiger Lake and DG1.
  static auto GetForTigerLakeDdi(i915_tgl::DdiId ddi_id) {
    ZX_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_A);
    ZX_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_TC_6);

    const int ddi_index = ddi_id - i915_tgl::DdiId::DDI_A;
    return hwreg::RegisterAddr<DdiAuxControl>(0x64010 + 0x100 * ddi_index);
  }
};

// DDI_AUX_DATA (DDI AUX Channel Data)
//
// Each DDI has 5 instances of the DDI_AUX_DATA register, making up a 20-byte
// buffer for storing AUX messages. The MMIO addresses for the 5 instances are
// consecutive.
//
// Tiger Lake: IHD-OS-TGL-Vol2c-12.21 Part 1 pages 346-351
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 1 pages 324-330
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 page 439
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 page 435
class DdiAuxData : public hwreg::RegisterBase<DdiAuxData, uint32_t> {
 public:
  // The most significant byte in each 32-bit register gets transmitted first.
  // Intel machines are little-endian, so the transmission order doesn't match
  // the memory order. The `swapped_` part of the name aims to draw attention
  // to this subtlety.
  //
  // The value is not meaningful while the corresponding DDI_AUX_CTL register's
  // `transaction_in_progress` field is true.
  DEF_FIELD(31, 0, swapped_bytes);

  // DDI_AUX_DATA_*_0 for the AUX channel with the given control register.
  //
  // The DDI_AUX_DATA_*_[1-4] data registers are accessed using direct MMIO
  // operations.
  //
  // We can get away with using DDI_AUX_CTL as the input because all DDI AUX
  // channels currently have the same MMIO layout. When this isn't the case
  // anymore, we'll replace this factory function with GetFor*Ddi() functions,
  // matching DdiAuxControl.
  static auto GetData0ForAuxControl(const DdiAuxControl& aux_control) {
    static constexpr uint32_t kAuxControlMmioBase = 0x64010;
    static constexpr uint32_t kAuxDataMmioBase = 0x64014;
    return hwreg::RegisterAddr<DdiAuxData>(aux_control.reg_addr() +
                                           (kAuxDataMmioBase - kAuxControlMmioBase));
  }
};

// DPCLKA_CFGCR0 (Display Clock Configuration Control Register 0?)
//
// This register controls which DPLL (Display PLL) is used as a clock source by
// each Combo DDI, and whether the DDI's clock is gated. Each Type C DDI has its
// own dedicated MGPLL, so this register only configures the clock gating for
// Type C DDIs.
//
// The Kaby Lake and Skylake equivalent of this register is
// `DisplayPllDdiMapKabyLake` (DPLL_CTRL2).
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 1, pages 608-610
// Lakefield: IHD-OS-LKF-Vol 2c-5.21 Part 1, pages 561-563
class DdiClockConfig : public hwreg::RegisterBase<DdiClockConfig, uint32_t> {
 public:
  DEF_BIT(24, ddi_c_clock_disabled);
  DEF_BIT(23, ddi_type_c_6_clock_disabled);
  DEF_BIT(22, ddi_type_c_5_clock_disabled);
  DEF_BIT(21, ddi_type_c_4_clock_disabled);

  DEF_BIT(14, ddi_type_c_3_clock_disabled);
  DEF_BIT(13, ddi_type_c_2_clock_disabled);
  DEF_BIT(12, ddi_type_c_1_clock_disabled);
  DEF_BIT(11, ddi_b_clock_disabled);
  DEF_BIT(10, ddi_a_clock_disabled);

  // If true, the DDI's clock is disabled. This is accomplished by gating.
  bool ddi_clock_disabled(i915_tgl::DdiId ddi_id) const {
    static constexpr int kComboBitIndices[] = {10, 11, 24};
    static constexpr int kTypeCBitIndices[] = {12, 13, 14, 21, 22, 23};
    int bit_index;
    if (ddi_id >= i915_tgl::DdiId::DDI_TC_1 && ddi_id <= i915_tgl::DdiId::DDI_TC_6) {
      const int ddi_index = ddi_id - i915_tgl::DdiId::DDI_TC_1;
      bit_index = kTypeCBitIndices[ddi_index];
    } else {
      ZX_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_A);
      ZX_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_C);
      const int ddi_index = ddi_id - i915_tgl::DdiId::DDI_A;
      bit_index = kComboBitIndices[ddi_index];
    }
    return static_cast<bool>(
        hwreg::BitfieldRef<const uint32_t>(reg_value_ptr(), bit_index, bit_index).get());
  }

  // See `ddi_clock_disabled()` for details.
  DdiClockConfig& set_ddi_clock_disabled(i915_tgl::DdiId ddi_id, bool clock_disabled) {
    static constexpr int kComboBitIndices[] = {10, 11, 24};
    static constexpr int kTypeCBitIndices[] = {12, 13, 14, 21, 22, 23};
    int bit_index;
    if (ddi_id >= i915_tgl::DdiId::DDI_TC_1 && ddi_id <= i915_tgl::DdiId::DDI_TC_6) {
      const int ddi_index = ddi_id - i915_tgl::DdiId::DDI_TC_1;
      bit_index = kTypeCBitIndices[ddi_index];
    } else {
      ZX_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_A);
      ZX_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_C);
      const int ddi_index = ddi_id - i915_tgl::DdiId::DDI_A;
      bit_index = kComboBitIndices[ddi_index];
    }
    hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit_index, bit_index).set(clock_disabled ? 1 : 0);
    return *this;
  }

  // Documented values for `ddi_*_clock_display_pll_select` fields.
  enum class DdiClockDisplayPllSelect {
    kDisplayPll0 = 0b00,
    kDisplayPll1 = 0b01,
    kDisplayPll4 = 0b10,
  };

  // These fields have a non-trivial representation. They should be used via the
  // `ddi_clock_display_pll()` and `set_ddi_clock_display_pll()` helpers.
  DEF_ENUM_FIELD(DdiClockDisplayPllSelect, 5, 4, ddi_c_clock_display_pll_select);
  DEF_ENUM_FIELD(DdiClockDisplayPllSelect, 3, 2, ddi_b_clock_display_pll_select);
  DEF_ENUM_FIELD(DdiClockDisplayPllSelect, 1, 0, ddi_a_clock_display_pll_select);

  // The DPLL (Display PLL) used as a clock source for a DDI.
  //
  // Returns DPLL_INVALID if the field is set to an undocumented value.
  Dpll ddi_clock_display_pll(i915_tgl::DdiId ddi_id) const {
    ZX_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_A);
    ZX_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_C);

    const int ddi_index = ddi_id - i915_tgl::DdiId::DDI_A;
    const int bit_index = ddi_index * 2;
    const auto dpll_select = static_cast<DdiClockDisplayPllSelect>(
        hwreg::BitfieldRef<const uint32_t>(reg_value_ptr(), bit_index + 1, bit_index).get());

    switch (dpll_select) {
      case DdiClockDisplayPllSelect::kDisplayPll0:
        return Dpll::DPLL_0;
      case DdiClockDisplayPllSelect::kDisplayPll1:
        return Dpll::DPLL_1;
      case DdiClockDisplayPllSelect::kDisplayPll4:
        // TODO(fxbug.dev/110351): Add support for DPLL4.
        break;
    }
    return Dpll::DPLL_INVALID;  // The field is set to an undocumented value.
  }

  // See `ddi_clock_display_pll()` for details.
  DdiClockConfig& set_ddi_clock_display_pll(i915_tgl::DdiId ddi_id, Dpll display_pll) {
    ZX_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_A);
    ZX_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_C);

    DdiClockDisplayPllSelect dpll_select;
    switch (display_pll) {
      case Dpll::DPLL_0:
        dpll_select = DdiClockDisplayPllSelect::kDisplayPll0;
        break;
      case Dpll::DPLL_1:
        dpll_select = DdiClockDisplayPllSelect::kDisplayPll1;
        break;

      // TODO(fxbug.dev/110351): Add support for DPLL4.
      default:
        ZX_DEBUG_ASSERT_MSG(false, "Invalid Display PLL: %d", display_pll);
        dpll_select = DdiClockDisplayPllSelect::kDisplayPll0;
    }

    const int ddi_index = ddi_id - i915_tgl::DdiId::DDI_A;
    const int bit_index = ddi_index * 2;
    hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit_index + 1, bit_index)
        .set(static_cast<uint32_t>(dpll_select));
    return *this;
  }

  static auto Get() { return hwreg::RegisterAddr<DdiClockConfig>(0x164280); }
};

// DDI_CLK_SEL
// Type C DDI Clock Selection
//
// Each Type-C DDI has 5 PLL inputs: Type-C PLL, and Thunderbolt PLL with 4
// different frequencies.
// Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev 2.0, Page 169 "PLL Arrangement"
//
// This register selects the clock source for a given Type-C DDI.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 1, Page 356-357
class TypeCDdiClockSelect : public hwreg::RegisterBase<TypeCDdiClockSelect, uint32_t> {
 public:
  // Select which clock to use for this DDI.
  // Valid values are listed below in `ClockSelect` enum class.
  //
  // Driver can use `clock_select` and `set_clock_select` helper methods to
  // read / write to this field.
  DEF_FIELD(31, 28, clock_select_raw);

  enum class ClockSelect : uint32_t {
    kNone = 0b0000,
    kTypeCPll = 0b1000,
    kThunderbolt162MHz = 0b1100,
    kThunderbolt270MHz = 0b1101,
    kThunderbolt540MHz = 0b1110,
    kThunderbolt810MHz = 0b1111,
  };

  // Helper method to read the `clock_select_raw` field and check its validity.
  std::optional<ClockSelect> clock_select() const {
    auto raw = clock_select_raw();
    if (IsValidClockSelect(raw)) {
      return static_cast<ClockSelect>(raw);
    }
    zxlogf(WARNING, "Invalid clock_select field: 0x%x", raw);
    return std::nullopt;
  }

  // Helper method to set the `clock_select_raw` field using a strongly-typed
  // enum class.
  SelfType& set_clock_select(ClockSelect clock) {
    return set_clock_select_raw(static_cast<ValueType>(clock));
  }

  static auto GetForDdi(i915_tgl::DdiId ddi_id) {
    // Register address at
    // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 1, Page 356
    switch (ddi_id) {
      case i915_tgl::DdiId::DDI_TC_1:
        return hwreg::RegisterAddr<SelfType>(0x4610C);
      case i915_tgl::DdiId::DDI_TC_2:
        return hwreg::RegisterAddr<SelfType>(0x46110);
      case i915_tgl::DdiId::DDI_TC_3:
        return hwreg::RegisterAddr<SelfType>(0x46114);
      case i915_tgl::DdiId::DDI_TC_4:
        return hwreg::RegisterAddr<SelfType>(0x46118);
      case i915_tgl::DdiId::DDI_TC_5:
        return hwreg::RegisterAddr<SelfType>(0x4611C);
      case i915_tgl::DdiId::DDI_TC_6:
        return hwreg::RegisterAddr<SelfType>(0x46120);
      default:
        ZX_ASSERT_MSG(false, "DDI_CLK_SEL: Invalid DDI %d", ddi_id);
    }
  }

 private:
  static bool IsValidClockSelect(uint32_t clock_select_raw) {
    switch (clock_select_raw) {
      case 0b0000:
      case 0b1000:
      case 0b1100:
      case 0b1101:
      case 0b1110:
      case 0b1111:
        return true;
      default:
        return false;
    }
  }
};

// DDI_AUX_MUTEX (DDI AUX Channel Mutex)
//
// This register is not documented on Tiger Lake or DG1.
//
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 pages 440-441
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 page 436-437
class DdiAuxMutex : public hwreg::RegisterBase<DdiAuxMutex, uint32_t> {
 public:
  // If true, the mutex is used to arbitrate AUX channel access.
  //
  // The mutex must be enabled and acquired if PSR 1/2 (Panel Self-Refresh) or
  // GTC (Global Time Code) are used. Otherwise, the mutex can remain disabled.
  DEF_BIT(31, enabled);

  // Reads acquire the mutex, writes release the mutex.
  //
  // Any read is an attempt to acquire the mutex. A successful attempt returns
  // true in this field. Once the driver acquires the mutex, it retains
  // ownership (reads continue to return true) until it explicitly releases the
  // mutex.
  //
  // This is a Write-Clear field. Writing true releases the mutex.
  //
  // The driver should release the mutex once it completes an AUX transaction,
  // so the hardware can use it as well.
  DEF_BIT(30, acquired);

  // We can get away with using DDI_AUX_CTL as the input because all DDI AUX
  // channels currently have the same MMIO layout. When this isn't the case
  // anymore, we'll replace this factory function with GetFor*Ddi() functions,
  // matching DdiAuxControl.
  static auto GetForAuxControl(const DdiAuxControl& aux_control) {
    static constexpr uint32_t kAuxControlMmioBase = 0x64010;
    static constexpr uint32_t kAuxMutexMmioBase = 0x6402c;
    return hwreg::RegisterAddr<DdiAuxData>(aux_control.reg_addr() +
                                           (kAuxMutexMmioBase - kAuxControlMmioBase));
  }
};

// DP_TP_CTL (DisplayPort Transport Control)
//
// Tiger Lake: IHD-OS-TGL-Vol2c-12.21 Part 1 pages 600-603
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 1 pages 572-575
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 1 pages 517-520
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 1 pages 515-518
class DpTransportControl : public hwreg::RegisterBase<DpTransportControl, uint32_t> {
 public:
  // If true, the DisplayPort transport function is enabled for the DDI.
  DEF_BIT(31, enabled);

  // If true, FEC (Forward Error Correction) coding is enabled.
  //
  // Must only be set to true after the `enabled` is set to true. Must only be
  // set to false after `enabled` is set to false.
  //
  // This field does not exist on Kaby Lake and Skylake.
  DEF_BIT(30, forward_error_correction_enabled_tiger_lake);

  // True for MST (Multi Stream) mode, false for SST (Single Stream) mode.
  //
  // Kaby Lake and Skylake DDI A (eDP) and DDI E do not support MST.
  //
  // Must match the mode in the Transcoder DDI Function Control registers. Must
  // not change while the DDI is enabled.
  DEF_BIT(27, is_multi_stream);

  // Forces MST ACT (Allocation Change Trigger) to be sent at the next link
  // frame boundary. After the ACT is sent (indicated by DP_TP_STATUS), the bit
  // can be reset and set again to force sending another ACT.
  DEF_BIT(25, force_allocation_change_trigger);

  // This field does not exist on Kaby Lake and Skylake.
  DEF_FIELD(20, 19, training_pattern4_tiger_lake);
  static constexpr int kTrainingPattern4a = 0;
  static constexpr int kTrainingPattern4b = 1;
  static constexpr int kTrainingPattern4c = 2;

  // True if enhanced framing is enabled for SST. Must be false in MST mode.
  //
  // Must not change while the DDI is enabled.
  DEF_BIT(18, sst_enhanced_framing);

  // Training pattern 1 must be selected when a port is enabled.
  //
  // To re-train a link, the port must be disabled and re-enabled (with
  // training pattern 1 selected).
  DEF_FIELD(10, 8, training_pattern);
  static constexpr int kTrainingPattern1 = 0;
  static constexpr int kTrainingPattern2 = 1;
  static constexpr int kIdlePattern = 2;
  static constexpr int kSendPixelData = 3;
  static constexpr int kTrainingPattern3 = 4;

  // Not supported on Kaby Lake and Skylake.
  static constexpr int kTrainingPattern4TigerLake = 5;

  // For eDP only. Must not change while the DDI is enabled.
  DEF_BIT(6, alternate_scrambler_reset);

  // For Kaby Lake and Skylake. The DisplayPort transport logic is in DDIs.
  static auto GetForKabyLakeDdi(i915_tgl::DdiId ddi_id) {
    ZX_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_A);
    ZX_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_E);

    const int ddi_index = ddi_id - i915_tgl::DdiId::DDI_A;
    return hwreg::RegisterAddr<DpTransportControl>(0x64040 + 0x100 * ddi_index);
  }

  // For Tiger Lake and DG1. The DisplayPort transport logic is in transcoders.
  static auto GetForTigerLakeTranscoder(i915_tgl::TranscoderId transcoder_id) {
    ZX_ASSERT(transcoder_id >= i915_tgl::TranscoderId::TRANSCODER_A);

    // TODO(fxbug.dev/109278): Allow transcoder D, once we support it.
    ZX_ASSERT(transcoder_id <= i915_tgl::TranscoderId::TRANSCODER_C);

    const int transcoder_index = transcoder_id - i915_tgl::TranscoderId::TRANSCODER_A;
    return hwreg::RegisterAddr<DpTransportControl>(0x60540 + 0x1000 * transcoder_index);
  }
};

// An instance of DdiRegs represents the registers for a particular DDI.
class DdiRegs {
 public:
  explicit DdiRegs(i915_tgl::DdiId ddi_id) : ddi_id_(ddi_id) {}

  hwreg::RegisterAddr<DdiBufferControl> BufferControl() {
    // This works for Kaby Lake too, because the Ddi integer mapping takes
    // advantage of MMIO address space.
    return DdiBufferControl::GetForTigerLakeDdi(ddi_id_);
  }
  hwreg::RegisterAddr<DpTransportControl> DpTransportControl() {
    // This does not work for Tiger Lake.
    return DpTransportControl::GetForKabyLakeDdi(ddi_id_);
  }
  hwreg::RegisterAddr<DdiPhyConfigEntry1> PhyConfigEntry1(int entry_index) const {
    return DdiPhyConfigEntry1::GetDdiInstance(ddi_id_, entry_index);
  }
  hwreg::RegisterAddr<DdiPhyConfigEntry2> PhyConfigEntry2(int entry_index) const {
    return DdiPhyConfigEntry2::GetDdiInstance(ddi_id_, entry_index);
  }

 private:
  template <class RegType>
  hwreg::RegisterAddr<RegType> GetReg() {
    return hwreg::RegisterAddr<RegType>(RegType::kBaseAddr + 0x100 * ddi_id_);
  }

  i915_tgl::DdiId ddi_id_;
};

}  // namespace tgl_registers

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_DDI_H_
