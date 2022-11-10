// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_DDI_PHY_TIGER_LAKE_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_DDI_PHY_TIGER_LAKE_H_

// Most fields in the PHY (Physical Layer) configuration registers are not
// sufficiently documented to be configured by driver authors. Plausible
// explanations are that the fields are only intended for DMC (display
// microcontroller) usage, or that their default values are the only supported
// values for correct hardware operation.  The register definitions below expand
// abbreviations in register and field names when we have guesses that we are
// reasonably confident in.
//
// The "spare" fields are considered reserved, as opposed to free for driver
// use. This assumption is supported by the PORT_TX_DW5 descriptions, where the
// "Disable 2tap" field (referenced in the initialization sequence) is marked as
// "ospare2".
//
// Some reserved fields are documented as MBZ (must be zero) on Tiger Lake and
// DG1, but PBC (preserve bit content) on Ice Lake. These fields are currently
// described as MBZ.

#include <lib/ddk/debug.h>
#include <zircon/assert.h>

#include <cstdint>

#include <hwreg/bitfields.h>

#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"

namespace tgl_registers {

// PHY_MISC (Miscellaneous Physical layer settings?)
//
// This register has bits that are reserved but not MBZ (must be zero). So, it
// can only be safely updated via read-modify-write operations.
//
// This register is not documented on Kaby Lake or Skylake.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 664
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 663-664
// Ice Lake: IHD-OS-ICLLP-Vol 2c-1.22-Rev2.0 Part 2 page 361
class PhyMisc : public hwreg::RegisterBase<PhyMisc, uint32_t> {
 public:
  // Undocumented semantics.
  //
  // This is likely a communication channel from the display engine driver to
  // the PHY logic.
  DEF_FIELD(31, 28, display_engine_to_io);

  // Undocumented semantics.
  //
  // This is likely a communication channel from the PHY logic to the display
  // engine driver.
  DEF_FIELD(27, 24, io_to_display_engine);

  // If true, the compensation resistors are powered down.
  //
  // The display engine driver sets this field, and the PHY logic acts on it.
  // This must be set to false before the DDI is enabled.
  DEF_BIT(23, compensation_resistors_powered_down);

  DEF_RSVDZ_FIELD(19, 0);

  static auto GetForDdi(i915_tgl::DdiId ddi_id) {
    ZX_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_A);
    ZX_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_C);

    const int ddi_index = ddi_id - i915_tgl::DdiId::DDI_A;
    return hwreg::RegisterAddr<PhyMisc>(0x64c00 + 4 * ddi_index);
  }
};

// Undocumented register PORT_CL_DW0 / PHY Common Lane config double-word 0?
//
// This definition is currently only used as a host for MmioAddressForDdi().
class PortCommonLane0 : public hwreg::RegisterBase<PortCommonLane0, uint32_t> {
 public:
  // Returns the base address of the PORT_CL_ configuration registers for a i915_tgl::DdiId.
  static constexpr uint32_t MmioAddressForDdi(i915_tgl::DdiId ddi_id) {
    ZX_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_A);
    ZX_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_C);
    const int ddi_index = ddi_id - i915_tgl::DdiId::DDI_A;
    // `kMmioAddress` can be static in C++20.
    constexpr uint32_t kMmioAddress[] = {0x162000, 0x6c000, 0x160000};
    return kMmioAddress[ddi_index];
  }
};

// PORT_CL_DW5 (PHY Common Lane config double-word 5?)
//
// "Common Lane" functionality is centralized across all lanes in a PHY, and
// placed in a single power gate.
//
// All the bits in this register are documented, so it is safe to update this
// register without reading it first.
//
// This register is not documented on Kaby Lake or Skylake.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 885-886
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 897-898
// Ice Lake: IHD-OS-ICLLP-Vol 2c-1.22-Rev2.0 Part 2 pages 553-554
class PortCommonLane5 : public hwreg::RegisterBase<PortCommonLane5, uint32_t> {
 public:
  // Undocumented semantics.
  DEF_FIELD(31, 24, force);

  DEF_RSVDZ_BIT(23);

  DEF_BIT(22, fuse_valid_reset);
  DEF_BIT(21, fuse_valid_override);
  DEF_BIT(20, fuse_repull);
  DEF_FIELD(19, 16, common_register_interface_clock_count_max);
  DEF_RSVDZ_BIT(15);

  // IOSF PD (Intel On-chip System Fabric Presence Detection) count.
  DEF_FIELD(14, 13, onchip_system_fabric_presence_detection_count);
  DEF_RSVDZ_BIT(12);
  DEF_FIELD(11, 9, onchip_system_fabric_clock_divider_select);

  // If true, all transmitters are programmed by writes to group addresses.
  DEF_BIT(8, downlink_broadcast_enable);

  DEF_RSVDZ_BIT(7);
  DEF_BIT(6, port_staggering_enabled);
  DEF_BIT(5, power_gate_staggering_control_disabled);
  DEF_BIT(4, common_lane_power_down_enabled);
  DEF_BIT(3, common_register_interface_clock_select);
  DEF_BIT(2, phy_power_ack_override);
  DEF_FIELD(1, 0, suspend_clock_config);

  static auto GetForDdi(i915_tgl::DdiId ddi_id) {
    return hwreg::RegisterAddr<PortCommonLane5>(PortCommonLane0::MmioAddressForDdi(ddi_id) + 5 * 4);
  }
};

// PORT_CL_DW10 (PHY Common Lane config double-word 10?)
//
// This register has bits that are reserved but not MBZ (must be zero). So, it
// can only be safely updated via read-modify-write operations.
//
// This register is not documented on Kaby Lake or Skylake.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 887-889
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 899-901
// Ice Lake: IHD-OS-ICLLP-Vol 2c-1.22-Rev2.0 Part 2 pages 555-556
class PortCommonLaneMainLinkPower
    : public hwreg::RegisterBase<PortCommonLaneMainLinkPower, uint32_t> {
 public:
  // Possible values for the `terminating_resistor_override` field.
  enum class TerminatingResistorOverride {
    k150Ohms = 0,
    k100Ohms = 1,
  };

  DEF_RSVDZ_FIELD(31, 27);

  DEF_FIELD(26, 25, power_gate_sequential_delay_override);

  // If false, `power_gate_sequential_delay_override` is ignored.
  DEF_BIT(24, power_gate_sequential_delay_override_valid);

  // HPVG (High Voltage Power Gate) for the MIPI DSI operating mode.
  //
  // On Ice Lake display engines with one common lane for all IOs, this bit
  // controls the HVPG (High-Voltage Power Gate) for DSI0 (MIPI A).
  //
  // On display engines without MIPI DSI support, this bit is ignored.
  DEF_BIT(23, high_voltage_power_gate_control);

  // Unused (Common Register Interface spare bit) on most display engines.
  //
  // On Ice Lake display engines with one common lane for all IOs, this bit
  // controls the HVPG (High-Voltage Power Gate) for DSI1 (MIPI C).
  DEF_BIT(22, high_voltage_power_gate_control_dsi_c);

  // CRI (Common Register Interface) spare bits.
  DEF_FIELD(21, 16, common_register_interface_ret);

  DEF_RSVDZ_FIELD(15, 12);

  // If true, the DDI's main link lane 3 is powered down.
  //
  // Some `power_down_lane*` field combinations are not supported. The
  // `set_powered_up_lanes()` helper is guaranteed to set valid combinations.
  DEF_BIT(7, power_down_lane3);

  // If true, the DDI's main link lane 2 is powered down.
  //
  // Some `power_down_lane*` field combinations are not supported. The
  // `set_powered_up_lanes()` helper is guaranteed to set valid combinations.
  DEF_BIT(6, power_down_lane2);

  // If true, the DDI's main link lane 1 is powered down.
  //
  // Some `power_down_lane*` field combinations are not supported. The
  // `set_powered_up_lanes()` helper is guaranteed to set valid combinations.
  DEF_BIT(5, power_down_lane1);

  // If true, the DDI's main link lane 0 is powered down.
  //
  // Some `power_down_lane*` field combinations are not supported. The
  // `set_powered_up_lanes()` helper is guaranteed to set valid combinations.
  DEF_BIT(4, power_down_lane0);

  // If false, `edp_power_optimized_mode_enabled` is ignored.
  //
  // Some `power_down_lane*` field combinations are not supported. The
  // `set_powered_up_lanes()` helper is guaranteed to set valid combinations.
  DEF_BIT(3, edp_power_optimized_mode_valid);

  // If true, enables a eDP (embedded DisplayPort) power-optimized mode.
  //
  // This field is ignored if `edp_power_optimized_mode_valid` is false. Setting
  // this to true must be accompanied by a specific voltage swing configuration.
  DEF_BIT(2, edp_power_optimized_mode_enabled);

  // If false, `terminating_resistor_override` is ignored.
  DEF_BIT(1, terminating_resistor_override_valid);

  // Overrides the terminating resisor value.
  DEF_ENUM_FIELD(TerminatingResistorOverride, 0, 0, terminating_resistor_override);

  // Powers up/down DDI main link lanes.
  //
  // `active_lane_count` must be a 1/2/4 for DisplayPort connections, and 4 for
  // HDMI connections. DSI connections are not currently supported.
  PortCommonLaneMainLinkPower& set_powered_up_lanes(int active_lane_count) {
    ZX_ASSERT(active_lane_count >= 1);
    ZX_ASSERT(active_lane_count <= 4);
    return set_power_down_lane0(false)
        .set_power_down_lane1(active_lane_count <= 1)
        .set_power_down_lane2(active_lane_count <= 2)
        .set_power_down_lane3(active_lane_count <= 3);
  }

  // Powers up/down DDI main link lanes for a reverse connection.
  //
  // `active_lane_count` must be a 1/2/4 for DisplayPort connections, and 4 for
  // HDMI connections. DSI connections are not currently supported.
  PortCommonLaneMainLinkPower& set_powered_up_lanes_reversed(int active_lane_count) {
    ZX_ASSERT(active_lane_count >= 1);
    ZX_ASSERT(active_lane_count <= 4);
    return set_power_down_lane3(false)
        .set_power_down_lane2(active_lane_count <= 1)
        .set_power_down_lane1(active_lane_count <= 2)
        .set_power_down_lane0(active_lane_count <= 3);
  }

  static auto GetForDdi(i915_tgl::DdiId ddi_id) {
    return hwreg::RegisterAddr<PortCommonLaneMainLinkPower>(
        PortCommonLane0::MmioAddressForDdi(ddi_id) + 10 * 4);
  }
};

// PORT_CL_DW12 (PHY Common Lane config double-word 12?)
//
// All the bits in this register are documented, so it is safe to update this
// register without reading it first.
//
// This register is not documented on Kaby Lake or Skylake.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 890-891
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 902-903
// Ice Lake: IHD-OS-ICLLP-Vol 2c-1.22-Rev2.0 Part 2 pages 557-559
class PortCommonLaneMiscPower : public hwreg::RegisterBase<PortCommonLaneMiscPower, uint32_t> {
 public:
  DEF_RSVDZ_FIELD(31, 30);
  DEF_BIT(29, mipi_lane_enabled);
  DEF_RSVDZ_BIT(28);

  // If false, `mipi_mode_override` is ignored.
  DEF_BIT(27, mipi_mode_override_valid);
  DEF_BIT(26, mipi_mode_override);
  DEF_RSVDZ_FIELD(25, 12);

  // Overrides the power request signal for the AUX channel.
  //
  // Ignored if `aux_power_request_override_valid` is false.
  DEF_BIT(11, aux_power_request_override);

  // If false, `aux_power_request_override` is ignored.
  DEF_BIT(10, aux_power_request_override_valid);

  DEF_RSVDZ_FIELD(9, 7);
  DEF_BIT(6, aux_phy_status);  // Read-only.
  DEF_RSVDZ_BIT(5);
  DEF_BIT(4, aux_power_acknowledged);  // Read-only.

  DEF_RSVDZ_FIELD(3, 1);

  // If true, the AUX lane will eventually be powered up.
  DEF_BIT(0, aux_lane_enabled);

  static auto GetForDdi(i915_tgl::DdiId ddi_id) {
    return hwreg::RegisterAddr<PortCommonLaneMiscPower>(PortCommonLane0::MmioAddressForDdi(ddi_id) +
                                                        12 * 4);
  }
};

// PORT_CL_DW15 (PHY Common Lane config double-word 15?)
//
// This register reports the state of powering various domains inside the PHY.
// All fields are read-only.
//
// This register is not documented on Kaby Lake or Skylake.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 892-893
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 904-905
// Ice Lake: IHD-OS-ICLLP-Vol 2c-1.22-Rev2.0 Part 2 pages 560-561
class PortCommonLanePowerStatus : public hwreg::RegisterBase<PortCommonLanePowerStatus, uint32_t> {
 public:
  DEF_RSVDZ_FIELD(31, 30);

  DEF_BIT(29, high_voltage_power_gate_power_acknowledged);
  DEF_BIT(28, high_voltage_power_gate_enabled);
  DEF_BIT(27, mipi_power_acknowledged);

  DEF_RSVDZ_FIELD(26, 22);

  DEF_BIT(21, aux_power_requested);
  DEF_RSVDZ_FIELD(20, 18);
  DEF_BIT(17, aux_power_acknowledged);

  DEF_RSVDZ_FIELD(16, 0);

  static auto GetForDdi(i915_tgl::DdiId ddi_id) {
    return hwreg::RegisterAddr<PortCommonLanePowerStatus>(
        PortCommonLane0::MmioAddressForDdi(ddi_id) + 15 * 4);
  }
};

// PORT_CL_DW16 (PHY Common Lane config double-word 16?)
//
// This register has bits that are reserved but not MBZ (must be zero). So, it
// can only be safely updated via read-modify-write operations.
//
// This register is not documented on Kaby Lake or Skylake.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 894-895
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 906-907
// Ice Lake: IHD-OS-ICLLP-Vol 2c-1.22-Rev2.0 Part 2 pages 562-563
class PortCommonLane16 : public hwreg::RegisterBase<PortCommonLane16, uint32_t> {
 public:
  DEF_RSVDZ_FIELD(31, 16);

  DEF_BIT(15, ddi_b_hd_port_select_override_valid);
  DEF_BIT(14, ddi_b_hd_port_select_override);
  DEF_BIT(13, ddi_c_hd_port_select_override_valid);
  DEF_BIT(12, ddi_c_hd_port_select_override);
  DEF_BIT(11, ddi_d_hd_port_select_override_valid);
  DEF_BIT(10, ddi_d_hd_port_select_override);

  DEF_RSVDZ_FIELD(9, 8);

  // If true, forces powering down the compensation source in the PHY.
  DEF_BIT(3, compensators_power_down_override);

  // If false, `compensators_power_down_override` is ignored.
  DEF_BIT(2, compensators_power_down_override_valid);

  // If true, force-wakes the CRI (Common Register Interface) domain.
  DEF_BIT(1, common_register_interface_wake_override);

  // If false, `common_register_interface_wake_override` is ignored.
  DEF_BIT(0, common_register_interface_wake_override_valid);

  static auto GetForDdi(i915_tgl::DdiId ddi_id) {
    return hwreg::RegisterAddr<PortCommonLane16>(PortCommonLane0::MmioAddressForDdi(ddi_id) +
                                                 16 * 4);
  }
};

// PORT_COMP_DW0 (PHY process variation Compensation config double-word 0?)
//
// This register has bits that are reserved but not MBZ (must be zero). So, it
// can only be safely updated via read-modify-write operations.
//
// This register is not documented on Kaby Lake or Skylake.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 896
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 908
// Ice Lake: IHD-OS-ICLLP-Vol 2c-1.22-Rev2.0 Part 2 page 564
class PortCompensation0 : public hwreg::RegisterBase<PortCompensation0, uint32_t> {
 public:
  // If true, the PHY's compensation resistors are initialized.
  DEF_BIT(31, initialized);

  DEF_FIELD(30, 29, transmitter_slew_control);
  DEF_FIELD(28, 27, transmitter_drive_switch_on);
  DEF_BIT(26, transmitter_drive_switch_control);

  DEF_BIT(23, process_monitor_clock_select);

  DEF_RSVDZ_FIELD(22, 20);

  // Programmable counter driving the frequency of compensation updates.
  DEF_FIELD(19, 8, periodic_counter);

  DEF_RSVDZ_FIELD(7, 0);

  static auto GetForDdi(i915_tgl::DdiId ddi_id) {
    return hwreg::RegisterAddr<PortCompensation0>(MmioAddressForDdi(ddi_id) + 0 * 4);
  }

  // Returns the base address of the PORT_COMP configuration registers.
  static constexpr uint32_t MmioAddressForDdi(i915_tgl::DdiId ddi_id) {
    ZX_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_A);
    ZX_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_C);
    const int ddi_index = ddi_id - i915_tgl::DdiId::DDI_A;
    // `kMmioAddress` can be static in C++20.
    constexpr uint32_t kMmioAddress[] = {0x162100, 0x6c100, 0x160100};
    return kMmioAddress[ddi_index];
  }
};

// PORT_COMP_DW1 (PHY process variation Compensation config double-word 1?)
//
// This register is not documented on Kaby Lake or Skylake.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 897-898
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 909-910
// Ice Lake: IHD-OS-ICLLP-Vol 2c-1.22-Rev2.0 Part 2 pages 565-566
class PortCompensation1 : public hwreg::RegisterBase<PortCompensation1, uint32_t> {
 public:
  DEF_BIT(31, low_dropout_regulator_bypass);
  DEF_BIT(30, frequency_compensation_override_valid);
  DEF_BIT(29, frequency_compensation_capacity_ratio);
  DEF_BIT(28, frequency_compensation_bias_select);
  DEF_FIELD(27, 26, frequency_compensation_input_select_overload);
  DEF_BIT(25, frequency_compensation_polarity_select);
  DEF_BIT(24, resistance_compensation_enabled);

  // TODO(fxbug.dev/114665): Add helpers for reading and writing the fields
  // below, which are spread across PortCompensation1,
  // PortCompensationNominalVoltageReferences, and
  // PortCompensationLowVoltageReferences.
  DEF_FIELD(23, 22, positive_nominal_voltage_reference_high_value_bits98);
  DEF_FIELD(21, 20, positive_nominal_voltage_reference_low_value_bits98);
  DEF_FIELD(19, 18, negative_nominal_voltage_reference_high_value_bits98);
  DEF_FIELD(17, 16, negative_nominal_voltage_reference_low_value_bits98);

  DEF_FIELD(15, 14, positive_high_voltage_reference_high_value_bits98);
  DEF_FIELD(13, 12, positive_high_voltage_reference_low_value_bits98);
  DEF_FIELD(11, 10, negative_high_voltage_reference_high_value_bits98);
  DEF_FIELD(9, 8, negative_high_voltage_reference_low_value_bits98);

  DEF_FIELD(7, 6, positive_low_voltage_reference_high_value_bits98);
  DEF_FIELD(5, 4, positive_low_voltage_reference_low_value_bits98);
  DEF_FIELD(3, 2, negative_low_voltage_reference_high_value_bits98);
  DEF_FIELD(1, 0, negative_low_voltage_reference_low_value_bits98);

  static auto GetForDdi(i915_tgl::DdiId ddi_id) {
    return hwreg::RegisterAddr<PortCompensation1>(PortCompensation0::MmioAddressForDdi(ddi_id) +
                                                  1 * 4);
  }
};

// PORT_COMP_DW3 (PHY process variation Compensation config double-word 3?)
//
// This register is not documented on Kaby Lake or Skylake.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 899-900
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 909-910
// Ice Lake: IHD-OS-ICLLP-Vol 2c-1.22-Rev2.0 Part 2 pages 567-568
class PortCompensationStatus : public hwreg::RegisterBase<PortCompensationStatus, uint32_t> {
 public:
  // Documented values for the `process_select` field.
  enum class ProcessSelect {
    kDot0 = 0b000,
    kDot1 = 0b001,
    kDot4 = 0b010,
  };

  // Documented values for the `voltage_select` field.
  enum class VoltageSelect {
    k850mv = 0b00,
    k950mv = 0b01,
    k1050mv = 0b10,
  };

  DEF_RSVDZ_FIELD(31, 29);

  // Process variation reported by the procmon (process monitor).
  //
  // The process monitor is a circuit that detects process skew (effects of
  // manufacturing variation) for the chip area that hosts the display engine.
  // The skew is characterized as slow, nominal, or fast.
  //
  // Sources:
  // * "Synergies Between Delay Test and Post-silicon Speed Path Validation:
  //   A Tutorial Introduction," 2021 IEEE European Test Symposium (ETS)
  // * "Use of Process monitors in Post silicon validation to reduce TTM,"
  //   2017 IEEE 35th VLSI Test Symposium (VTS)
  DEF_ENUM_FIELD(ProcessSelect, 28, 26, process_select);

  // The port's operating voltage.
  DEF_ENUM_FIELD(VoltageSelect, 25, 24, voltage_select);

  DEF_BIT(23, pll_ddi_power_acknowledged);
  DEF_BIT(22, first_compensation_done);
  DEF_BIT(21, process_monitor_done);

  DEF_BIT(20, current_compensation_code_maxout);
  DEF_BIT(19, current_compensation_code_minout);
  DEF_RSVDZ_FIELD(18, 15);
  DEF_FIELD(14, 8, current_compensation_code);

  DEF_BIT(7, mipi_low_power_data_negative_code_maxout);
  DEF_BIT(6, mipi_low_power_data_negative_code_minout);

  // LPDn (negative Data pin in Low-Power mode) compensation value.
  DEF_FIELD(5, 0, mipi_low_power_data_negative_code);

  static auto GetForDdi(i915_tgl::DdiId ddi_id) {
    return hwreg::RegisterAddr<PortCompensationStatus>(
        PortCompensation0::MmioAddressForDdi(ddi_id) + 3 * 4);
  }
};

// PORT_COMP_DW8 (PHY process variation Compensation config double-word 8?)
//
// All the bits in this register are documented, so it is safe to update this
// register without reading it first.
//
// This register is not documented on Kaby Lake or Skylake.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 901
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 914
// Ice Lake: IHD-OS-ICLLP-Vol 2c-1.22-Rev2.0 Part 2 pages 569-570
class PortCompensationSource : public hwreg::RegisterBase<PortCompensationSource, uint32_t> {
 public:
  DEF_RSVDZ_FIELD(31, 25);

  // Must be true for PHYs that serve as compensation sources.
  DEF_BIT(24, generate_internal_references);

  DEF_RSVDZ_FIELD(23, 15);

  // If true, periodic ICOMP (current compensation) value updates are disabled.
  DEF_BIT(14, periodic_current_compensation_disabled);

  DEF_RSVDZ_FIELD(13, 0);

  static auto GetForDdi(i915_tgl::DdiId ddi_id) {
    return hwreg::RegisterAddr<PortCompensationSource>(
        PortCompensation0::MmioAddressForDdi(ddi_id) + 8 * 4);
  }
};

// PORT_COMP_DW9 (PHY process variation Compensation config double-word 9?)
//
// This register stores the low bits of {negative, positive} {low, high}
// reference values for nominal voltage transistors. The high bits are in
// PORT_COMP_DW1.
//
// All the bits in this register are documented, so it is safe to update this
// register without reading it first.
//
// This register is not documented on Kaby Lake or Skylake.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 902
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 915
// Ice Lake: IHD-OS-ICLLP-Vol 2c-1.22-Rev2.0 Part 2 page 571
class PortCompensationNominalVoltageReferences
    : public hwreg::RegisterBase<PortCompensationNominalVoltageReferences, uint32_t> {
 public:
  // The high bits for all these values are in PORT_COMP_DW1.

  DEF_FIELD(31, 24, negative_nominal_voltage_reference_low_value_bits70);
  DEF_FIELD(23, 16, negative_nominal_voltage_reference_high_value_bits70);
  DEF_FIELD(15, 8, positive_nominal_voltage_reference_low_value_bits70);
  DEF_FIELD(7, 0, positive_nominal_voltage_reference_high_value_bits70);

  static auto GetForDdi(i915_tgl::DdiId ddi_id) {
    return hwreg::RegisterAddr<PortCompensationNominalVoltageReferences>(
        PortCompensation0::MmioAddressForDdi(ddi_id) + 9 * 4);
  }
};

// PORT_COMP_DW10 (PHY process variation Compensation config double-word 10?)
//
// This register stores the low bits of {negative, positive} {low, high}
// reference values for LVT (low voltage transistors). The high bits are in
// PORT_COMP_DW1.
//
// All the bits in this register are documented, so it is safe to update this
// register without reading it first.
//
// This register is not documented on Kaby Lake or Skylake.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 903
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 916
// Ice Lake: IHD-OS-ICLLP-Vol 2c-1.22-Rev2.0 Part 2 page 572
class PortCompensationLowVoltageReferences
    : public hwreg::RegisterBase<PortCompensationLowVoltageReferences, uint32_t> {
 public:
  // The high bits for all these values are in PORT_COMP_DW1.

  DEF_FIELD(31, 24, negative_low_voltage_reference_low_value_bits70);
  DEF_FIELD(23, 16, negative_low_voltage_reference_high_value_bits70);
  DEF_FIELD(15, 8, positive_low_voltage_reference_low_value_bits70);
  DEF_FIELD(7, 0, positive_low_voltage_reference_high_value_bits70);

  static auto GetForDdi(i915_tgl::DdiId ddi_id) {
    return hwreg::RegisterAddr<PortCompensationLowVoltageReferences>(
        PortCompensation0::MmioAddressForDdi(ddi_id) + 10 * 4);
  }
};

// Identifies a pair of pins used in voltage differential transmission.
//
// The lane usage is documented in the "Mode Set" > "Sequences for MIPI DSI" >
// "DSI Transcoder Enable Sequence" section of the display engine PRMs.
// Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0 page 127
// Ice Lake: IHD-OS-ICLLP-Vol 12-1.22-Rev2.0 page 129
enum class PortLane {
  kAux = 0x3,            // DisplayPort AUX channel. DSI Data lane 0.
  kAll = 0x6,            // Virtual pair that routes writes to all non-AUX lanes.
  kMainLinkLane0 = 0x8,  // 1st DisplayPort main link lane. DSI Data lane 1.
  kMainLinkLane1 = 0x9,  // 2nd DisplayPort main link lane. DSI Data lane 2.
  kMainLinkLane2 = 0xa,  // 3rd DisplayPort main link lane. DSI Clock lane.
  kMainLinkLane3 = 0xb,  // 4th DisplayPort main link lane. DSI Data lane 3.
};

// PORT_PCS_DW1 (Physical Coding Sublayer config double-word 1?)
//
// This register has bits that are reserved but not MBZ (must be zero). So, it
// can only be safely updated via read-modify-write operations.
//
// This register is not documented on Kaby Lake or Skylake.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 903-907
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 917-921
// Ice Lake: IHD-OS-ICLLP-Vol 2c-1.22-Rev2.0 Part 2 pages 573-575
class PortPhysicalCoding1 : public hwreg::RegisterBase<PortPhysicalCoding1, uint32_t> {
 public:
  // Possible values for the `duty_cycle_correction_schedule_select` field.
  enum class DutyCycleCorrectionScheduleSelect {
    kOnce = 0b00,
    kEvery100Microseconds = 0b01,
    kEvery1000Microseconds = 0b10,
    kContinuously = 0b11,
  };

  DEF_RSVDZ_FIELD(31, 29);

  DEF_BIT(28, common_mode_keeper_enabled_while_power_gated);

  // If true, the pins are power-gated (powered off).
  DEF_BIT(27, power_gate_powered_down);

  // Enables the common mode voltage keeper circuit.
  //
  // The common keeper preserves the common-mode voltage between the pair of
  // pins during low power modes.
  DEF_BIT(26, common_mode_keeper_enabled);
  DEF_FIELD(25, 24, common_mode_keeper_bias_control);

  DEF_RSVDZ_FIELD(23, 22);

  // Selects how often DCC (Duty Cycle Correction) is performed.
  DEF_ENUM_FIELD(DutyCycleCorrectionScheduleSelect, 21, 20, duty_cycle_correction_schedule_select);

  // If true, the DCC (Duty Cycle Correction) calibration is bypassed.
  //
  // Setting this to true also bypasses DFx (design for debug/test) receiver
  // calibration. The two bypasses share a signal in the PCS (Physical Coding
  // Sublayer).
  DEF_BIT(19, duty_cycle_correction_calibration_bypassed);

  // If true, DCC calibration will be performed on the next power up.
  //
  // Setting this to true forces a DCC (Duty Cycle Correction) calibration the
  // next time the DL (downlink) is woken up after a power down event.
  DEF_BIT(18, duty_cycle_correction_calibration_on_wake);

  // If true, forces a transmitter DCC (Duty Cycle Correction) calibration.
  //
  // This field should only be used (set to true) after the boot-time
  // initialization completes.
  DEF_BIT(17, force_transmitter_duty_cycle_correction_calibration);

  DEF_RSVDZ_FIELD(15, 14);

  DEF_FIELD(13, 12, transmitter_high);

  DEF_RSVDZ_FIELD(11, 10);

  DEF_FIELD(9, 8, clock_request);

  // If true, the lane's symbol clock is the TBC (Transmitter Buffer Clock).
  DEF_BIT(7, use_transmitter_buffer_clock_as_symbol_clock);

  // If false, `transmitter_fifo_reset_main_override` is ignored.
  DEF_BIT(6, transmitter_fifo_reset_main_override_valid);

  // Reset Main override for the transmitter's FIFO.
  //
  // Ignored if `transmitter_fifo_reset_main_override_valid` is false
  DEF_BIT(5, transmitter_fifo_reset_main_override);

  DEF_BIT(4, transmitter_deemphasis_value);

  DEF_FIELD(3, 2, latency_optimization_value);

  // If true, `soft_lane_reset` is read by the circuitry.
  DEF_BIT(1, soft_lane_reset_valid);

  // If false, requests that the lanes controlled by this register are reset.
  //
  // This field is only used if `soft_lane_reset_valid` is true.
  DEF_BIT(0, soft_lane_reset);

  static auto GetForDdiLane(i915_tgl::DdiId ddi_id, PortLane lane) {
    return hwreg::RegisterAddr<PortPhysicalCoding1>(MmioAddressForDdiLane(ddi_id, lane) + 1 * 4);
  }

  // Returns the base address of lane's PORT_PCS_ configuration registers.
  static constexpr uint32_t MmioAddressForDdiLane(i915_tgl::DdiId ddi_id, PortLane lane) {
    ZX_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_A);
    ZX_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_C);
    const int ddi_index = ddi_id - i915_tgl::DdiId::DDI_A;
    // `kMmioAddress` can be static in C++20.
    constexpr uint32_t kMmioAddress[] = {0x162000, 0x6c000, 0x160000};
    return kMmioAddress[ddi_index] | (static_cast<uint32_t>(lane) << 8);
  }
};

// PORT_PCS_DW9 (Physical Coding Sublayer config double-word 9?)
//
// All reserved bits in this register are MBZ (must be zero). So, the register
// can be safely updated without reading it first.
//
// This register is not documented on Kaby Lake or Skylake.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 908-910
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 922-925
// Ice Lake: IHD-OS-ICLLP-Vol 2c-1.22-Rev2.0 Part 2 pages 576-579
class PortPhysicalCoding9 : public hwreg::RegisterBase<PortPhysicalCoding9, uint32_t> {
 public:
  DEF_RSVDZ_FIELD(31, 28);

  DEF_FIELD(27, 16, strong_cm_count_overload);

  DEF_RSVDZ_FIELD(15, 11);

  DEF_FIELD(10, 8, stagger_multiplier);

  DEF_RSVDZ_FIELD(7, 6);

  DEF_BIT(5, stagger_override_valid);
  DEF_FIELD(4, 0, stagger_override);

  static auto GetForDdiLane(i915_tgl::DdiId ddi_id, PortLane lane) {
    return hwreg::RegisterAddr<PortPhysicalCoding9>(
        PortPhysicalCoding1::MmioAddressForDdiLane(ddi_id, lane) + 9 * 4);
  }
};

// PORT_TX_DW0 (Transmitter analog front-end config double-word 0?)
//
// This register controls transmitter equalization in the Combo PHY's AFE
// (Analog Front-End).
//
// All reserved bits in this register are MBZ (must be zero). So, the register
// can be safely updated without reading it first.
//
// This register is not documented on Kaby Lake or Skylake.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 929-931
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 945-948
class PortTransmitterMipiEqualization
    : public hwreg::RegisterBase<PortTransmitterMipiEqualization, uint32_t> {
 public:
  // Selects the equalization level for MIPI DSI transmission.
  //
  // This bit is ignored unless `mipi_equalization_override` is true.
  //
  // Low level equalization is 3.5 dB. High level equalization is 7 dB.
  DEF_BIT(31, mipi_equalization_is_high);

  // If true, lane equalization for MIPI DSI transmission is enabled.
  //
  // This bit is ignored unless `mipi_equalization_override` is true.
  DEF_BIT(30, mipi_equalization_enabled);

  // Transmitter equalization tap C+1 (post-cursor) coefficient.
  //
  // The PRM advises against changing this field. The default value is 0xb.
  DEF_FIELD(29, 24, post_cursor_coefficient);

  // If true, the equalization logic is driven by fields in this register.
  //
  // If this field is false, the equalization logic is driven by PPI (PHY
  // Protocol Interface, in the MIPI D-PHY specification) Transmitter
  // Equalization pins (TxEqActiveHS, TxEqLevelHS).
  DEF_BIT(23, mipi_equalization_override);

  DEF_RSVDZ_FIELD(22, 6);

  // Transmitter equalization tap C (cursor) coefficient.
  //
  // The PRM advises against changing this field. The default value is 0x34.
  DEF_FIELD(5, 0, cursor_coefficient);

  static auto GetForDdiLane(i915_tgl::DdiId ddi_id, PortLane lane) {
    return hwreg::RegisterAddr<PortTransmitterMipiEqualization>(
        MmioAddressForDdiLane(ddi_id, lane) + 0 * 4);
  }

  // Returns the base address of lane's PORT_TX_ configuration registers.
  static constexpr uint32_t MmioAddressForDdiLane(i915_tgl::DdiId ddi_id, PortLane lane) {
    ZX_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_A);
    ZX_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_C);
    const int ddi_index = ddi_id - i915_tgl::DdiId::DDI_A;
    // `kMmioAddress` can be static in C++20.
    constexpr uint32_t kMmioAddress[] = {0x162080, 0x6c080, 0x160080};
    return kMmioAddress[ddi_index] | (static_cast<uint32_t>(lane) << 8);
  }
};

using PortTransmitter0 = PortTransmitterMipiEqualization;

// PORT_TX_DW1 (Transmitter analog front-end config double-word 1?)
//
// All reserved bits in this register are MBZ (must be zero). So, the register
// can be safely updated without reading it first.
//
// This register is not documented on Kaby Lake or Skylake.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 932-934
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 949-952
// Ice Lake: IHD-OS-ICLLP-Vol 2c-1.22-Rev2.0 Part 2 page 614
class PortTransmitter1 : public hwreg::RegisterBase<PortTransmitter1, uint32_t> {
 public:
  DEF_RSVDZ_FIELD(31, 8);

  // ICOMP (current configuration) reference configuration.
  //
  // This configuration bit is routed from the COMP (compensation) registers to
  // the TX (Transmitter analog front-end) registers.
  DEF_BIT(7, output_current_compensation_reference_config);

  // Sets the transmitter's current intensity boost ratio.
  DEF_FIELD(6, 5, output_current_reference_control);

  // Configures the MIPI DSI HSTX (high-speed transmission mode) slew.
  DEF_FIELD(4, 3, mipi_high_speed_transmission_slew_rate_control);

  // Enables the LDO feedback path for low reference voltage.
  DEF_BIT(2, low_reference_voltage_low_dropout_regulator_feedback_enabled);

  // Enables the LDO feedback path for high reference voltage.
  DEF_BIT(1, high_reference_voltage_low_dropout_regulator_feedback_enabled);

  // Enables the LDO feedback path for nominal reference voltage.
  DEF_BIT(0, nominal_reference_voltage_low_dropout_regulator_feedback_enabled);

  static auto GetForDdiLane(i915_tgl::DdiId ddi_id, PortLane lane) {
    return hwreg::RegisterAddr<PortTransmitter1>(
        PortTransmitter0::MmioAddressForDdiLane(ddi_id, lane) + 1 * 4);
  }
};

// PORT_TX_DW2 (Transmitter analog front-end config double-word 2?)
//
// All reserved bits in this register are MBZ (must be zero). So, the register
// can be safely updated without reading it first.
//
// This register is not documented on Kaby Lake or Skylake.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 935-937
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 953-956
// Ice Lake: IHD-OS-ICLLP-Vol 2c-1.22-Rev2.0 Part 2 pages 615-617
class PortTransmitterVoltageSwing
    : public hwreg::RegisterBase<PortTransmitterVoltageSwing, uint32_t> {
 public:
  DEF_RSVDZ_FIELD(31, 16);

  // This field must be combined with `voltage_swing_select_bits20`. The helpers
  // `voltage_swing_select()` and `set_voltage_swing_select()` handle that.
  DEF_BIT(15, voltage_swing_select_bit3);

  DEF_BIT(14, weak_common_mode_select);

  // This field must be combined with `voltage_swing_select_bits3`. The helpers
  // `voltage_swing_select()` and `set_voltage_swing_select()` handle that.
  DEF_FIELD(13, 11, voltage_swing_select_bits20);

  DEF_FIELD(10, 8, force_latency_optimized_fifo);

  // Applied to RCOMP (resistance compensation) code.
  //
  // This field adjusts the RCOMP code to get the desired output termination
  // resistance. This field is also named the (voltage) swing scalar.
  DEF_FIELD(7, 0, resistance_compensation_code_scalar);

  // Configures the signal's peak-to-peak voltage differences.
  //
  // There is an undocumented mapping between (transition and non-transition)
  // peak-to-peak voltage differences and values in this field. Intel's
  // documentation has tables mapping voltage swing and pre-emphasis levels to
  // field values.
  int8_t voltage_swing_select() const {
    return static_cast<int8_t>((static_cast<int8_t>(voltage_swing_select_bit3()) << 3) |
                               static_cast<int8_t>(voltage_swing_select_bits20()));
  }

  // See `voltage_swing_select()` for details.
  PortTransmitterVoltageSwing& set_voltage_swing_select(int voltage_swing_select) {
    ZX_DEBUG_ASSERT(voltage_swing_select >= 0);
    ZX_DEBUG_ASSERT(voltage_swing_select <= 0b1111);
    return set_voltage_swing_select_bits20(static_cast<uint32_t>(voltage_swing_select & 7))
        .set_voltage_swing_select_bit3(static_cast<uint32_t>((voltage_swing_select >> 3) & 1));
  }

  static auto GetForDdiLane(i915_tgl::DdiId ddi_id, PortLane lane) {
    return hwreg::RegisterAddr<PortTransmitterVoltageSwing>(
        PortTransmitter0::MmioAddressForDdiLane(ddi_id, lane) + 2 * 4);
  }
};

// PORT_TX_DW4 (Transmitter analog front-end config double-word 4?)
//
// This register has bits that are reserved but not MBZ (must be zero). So, it
// can only be safely updated via read-modify-write operations.
//
// This register is not documented on Kaby Lake or Skylake.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 938-940
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 957-960
// Ice Lake: IHD-OS-ICLLP-Vol 2c-1.22-Rev2.0 Part 2 pages 618-620
class PortTransmitterEqualization
    : public hwreg::RegisterBase<PortTransmitterEqualization, uint32_t> {
 public:
  DEF_BIT(31, load_generation_select);

  DEF_BIT(23, bs_comp_override);

  DEF_FIELD(22, 18, termination_resistance_limit);

  // Equalization tap C+1 (post-cursor) coefficient.
  DEF_FIELD(17, 12, post_cursor_coefficient1);

  // Equalization tap C+2 (post-cursor) coefficient.
  DEF_FIELD(11, 6, post_cursor_coefficient2);

  // Equalization tap C (cursor) coefficient.
  DEF_FIELD(5, 0, cursor_coefficient);

  static auto GetForDdiLane(i915_tgl::DdiId ddi_id, PortLane lane) {
    return hwreg::RegisterAddr<PortTransmitterEqualization>(
        PortTransmitter0::MmioAddressForDdiLane(ddi_id, lane) + 4 * 4);
  }
};

// PORT_TX_DW5 (Transmitter analog front-end config double-word 5?)
//
// This register has bits that are reserved but not MBZ (must be zero). So, it
// can only be safely updated via read-modify-write operations.
//
// This register is not documented on Kaby Lake or Skylake.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 941-944
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 961-964
// Ice Lake: IHD-OS-ICLLP-Vol 2c-1.22-Rev2.0 Part 2 pages 621-624
class PortTransmitterVoltage : public hwreg::RegisterBase<PortTransmitterVoltage, uint32_t> {
 public:
  // While true, the lane's voltage parameters cannot be reconfigured.
  //
  // This field must be set to false briefly for the parameters in the PORT_TX*
  // registers to be picked up, then set back to true.
  DEF_BIT(31, training_enabled);

  DEF_BIT(30, two_tap_equalization_disabled);
  DEF_BIT(29, three_tap_equalization_disabled);

  DEF_BIT(26, cursor_programming_disabled);
  DEF_BIT(25, coefficient_polarity_disabled);

  DEF_RSVDZ_FIELD(23, 21);

  DEF_FIELD(20, 18, scaling_mode_select);
  DEF_FIELD(17, 16, decode_timer_select);
  DEF_FIELD(15, 11, cr_scaling_coefficient);

  DEF_FIELD(5, 3, terminating_resistor_select);

  static auto GetForDdiLane(i915_tgl::DdiId ddi_id, PortLane lane) {
    return hwreg::RegisterAddr<PortTransmitterVoltage>(
        PortTransmitter0::MmioAddressForDdiLane(ddi_id, lane) + 5 * 4);
  }
};

// PORT_TX_DW6 (Transmitter analog front-end config double-word 6?)
//
// All reserved bits in this register are MBZ (must be zero). So, the register
// can be safely updated without reading it first.
//
// This register is not documented on Kaby Lake or Skylake.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 945-947
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 965-968
// Ice Lake: IHD-OS-ICLLP-Vol 2c-1.22-Rev2.0 Part 2 page 625
class PortTransmitterLowDropoutRegulator
    : public hwreg::RegisterBase<PortTransmitterLowDropoutRegulator, uint32_t> {
 public:
  DEF_RSVDZ_FIELD(31, 8);

  DEF_BIT(7, function_override_enabled);

  // This field should be replicated from CRI (Common Register Interface).
  DEF_FIELD(6, 1, low_dropout_reference_select);

  // This field should be replicated from CRI (Common Register Interface).
  DEF_BIT(0, low_dropout_bypass);

  static auto GetForDdiLane(i915_tgl::DdiId ddi_id, PortLane lane) {
    return hwreg::RegisterAddr<PortTransmitterLowDropoutRegulator>(
        PortTransmitter0::MmioAddressForDdiLane(ddi_id, lane) + 6 * 4);
  }
};

// PORT_TX_DW7 (Transmitter analog front-end config double-word 7?)
//
// This register has bits that are reserved but not MBZ (must be zero). So, it
// can only be safely updated via read-modify-write operations.
//
// This register is not documented on Kaby Lake or Skylake.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 948-950
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 969-971
// Ice Lake: IHD-OS-ICLLP-Vol 2c-1.22-Rev2.0 Part 2 pages 626-628
class PortTransmitterNScalar : public hwreg::RegisterBase<PortTransmitterNScalar, uint32_t> {
 public:
  DEF_FIELD(30, 24, n_scalar);

  static auto GetForDdiLane(i915_tgl::DdiId ddi_id, PortLane lane) {
    return hwreg::RegisterAddr<PortTransmitterNScalar>(
        PortTransmitter0::MmioAddressForDdiLane(ddi_id, lane) + 7 * 4);
  }
};

// PORT_TX_DW8 (Transmitter analog front-end config double-word 8?)
//
// This register has bits that are reserved but not MBZ (must be zero). So, it
// can only be safely updated via read-modify-write operations.
//
// This register is not documented on Kaby Lake or Skylake.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 951-953
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 972-975
class PortTransmitterDutyCycleCorrection
    : public hwreg::RegisterBase<PortTransmitterDutyCycleCorrection, uint32_t> {
 public:
  // Possible values for `duty_cycle_correction_clock_divider_select`.
  enum class ClockDividerSelect {
    k2 = 0b01,
    k4 = 0b10,
    k8 = 0b11,
  };

  DEF_BIT(31, output_duty_cycle_correction_clock_select);
  DEF_ENUM_FIELD(ClockDividerSelect, 30, 29, output_duty_cycle_correction_clock_divider_select);

  // Ignored if `output_duty_cycle_correction_code_override_valid` is false.
  DEF_FIELD(28, 24, output_duty_cycle_correction_code_override);

  // If false, `output_duty_cycle_correction_code_override` is ignored.
  DEF_BIT(23, output_duty_cycle_correction_code_override_valid);

  DEF_BIT(22, output_duty_cycle_correction_fuse_enabled);
  DEF_FIELD(20, 16, output_duty_cycle_correction_lower_limit);

  DEF_FIELD(14, 13, input_duty_cycle_correction_thermal_bits43);
  DEF_FIELD(12, 8, input_duty_cycle_correction_code);
  DEF_FIELD(7, 5, input_duty_cycle_correction_thermal_bits20);

  DEF_FIELD(4, 0, output_duty_cycle_correction_upper_limit);

  static auto GetForDdiLane(i915_tgl::DdiId ddi_id, PortLane lane) {
    return hwreg::RegisterAddr<PortTransmitterDutyCycleCorrection>(
        PortTransmitter0::MmioAddressForDdiLane(ddi_id, lane) + 8 * 4);
  }
};

}  // namespace tgl_registers

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_DDI_PHY_TIGER_LAKE_H_
