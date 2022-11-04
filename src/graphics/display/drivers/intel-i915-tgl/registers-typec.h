// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_TYPEC_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_TYPEC_H_

#include <lib/ddk/debug.h>
#include <lib/stdcompat/bit.h>
#include <zircon/assert.h>

#include <optional>

#include <hwreg/bitfields.h>

#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"

namespace tgl_registers {

// ===========================================================================
//                             Type-C FIA Registers
// ===========================================================================
// TODO(fxbug.dev/110198): Consider moving these register definitions into a
// separated file.
//
// The Flexi I/O Adapter (FIA) muxes data and clocks between the USB-Type C PHY
// and multiple controllers, including Display Engine (DE) controllers.
//
// When a new device is connected over the display controller, the IOM [1]
// (Type-C subsystem IO manager) programs the FIA registers with pin assignment,
// link width, live state etc before notifying display engine about the new
// display. The display driver handshakes with the IOM by writing to the FIA
// registers on connection / disconnection.
//
// Each FIA register manages physical connectors that connect to that specific
// FIA; the mapping of global Type-C port ID to FIA ID and FIA internal
// connector ID is available at:
// Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0 "TypeC Programming" > "Port
// Mapping" table, Page 400.
//
// [1] Intel's documentation also refers to the IOM (Type-C subsystem IO
// manager) as the SOC uC (system-on-chip microcontroller). Besides, the USB-C
// PD FW (power delivery engine firmware) may use the FIA registers as well to
// configure PHY lanes and determine the ownership of Type-C connectors.

// PORT_TX_DFLEXDPMLE1
// Dynamic FlexIO DisplayPort Main-Link Lane Enable 1 (for Type-C Connector 0-7)
// (?)
//
// This FIA register is used for drivers to tell FIA hardware which main link
// lanes of DisplayPort are enabled on each Type-C connector.
//
// Notes:
//
// 1. The connector ID here is the logical number for each FIA, and the Type-C
// port to FIA connector ID mapping is available at:
// Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0 "TypeC Programming" > "Port
// Mapping" table, Page 400.
//
// 2. The display driver may only change this register when the DisplayPort
// controller is in safe mode (see
// `DynamicFlexIoDisplayPortControllerSafeStateSettings`).
//
// 3. Intel Graphics Programmer's reference manual (register definitions, and
// display engine) also uses "main links" in this register's definition to
// refer to the DisplayPort main-link lanes (also known as "DisplayPort lanes").
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 2, Pages 913-915.
class DynamicFlexIoDisplayPortMainLinkLaneEnabled
    : public hwreg::RegisterBase<DynamicFlexIoDisplayPortMainLinkLaneEnabled, uint32_t> {
 public:
  // Indicates whether DisplayPort Main link lane 3 (ML3) is enabled on
  // connector 1.
  //
  // Drivers can use helper method `enabled_main_links_bits`,
  // `set_enabled_main_links_bits` to get / set main link status bitmap for a
  // given DDI.
  //
  // The register has these bit fields for Connector 0 to 7. Since on Tiger
  // Lake each FIA only connects to two connectors, we only define the bits for
  // connector 0 and 1.
  DEF_BIT(7, connector_1_display_port_main_link_lane_3_enabled);

  // Indicates whether DisplayPort Main link lane 2 (ML2) is enabled on
  // connector 1.
  DEF_BIT(6, connector_1_display_port_main_link_lane_2_enabled);

  // Indicates whether DisplayPort Main link lane 1 (ML1) is enabled on
  // connector 1.
  DEF_BIT(5, connector_1_display_port_main_link_lane_1_enabled);

  // Indicates whether DisplayPort Main link lane 0 (ML0) is enabled on
  // connector 1.
  DEF_BIT(4, connector_1_display_port_main_link_lane_0_enabled);

  // Indicates whether DisplayPort Main link lane 3 (ML3) is enabled on
  // connector 0.
  DEF_BIT(3, connector_0_display_port_main_link_lane_3_enabled);

  // Indicates whether DisplayPort Main link lane 2 (ML2) is enabled on
  // connector 0.
  DEF_BIT(2, connector_0_display_port_main_link_lane_2_enabled);

  // Indicates whether DisplayPort Main link lane 1 (ML1) is enabled on
  // connector 0.
  DEF_BIT(1, connector_0_display_port_main_link_lane_1_enabled);

  // Indicates whether DisplayPort Main link lane 0 (ML0) is enabled on
  // connector 0.
  DEF_BIT(0, connector_0_display_port_main_link_lane_0_enabled);

  // Getter of `connector_1_display_port_main_link_lane_{0,1,2,3}_enabled` and
  // `connector_0_display_port_main_link_lane_{0,1,2,3}_enabled` fields above
  // based on `ddi_id`.
  //
  // Callers must make sure they read from the correct FIA register.
  uint32_t enabled_display_port_main_link_lane_bits(i915_tgl::DdiId ddi_id) const {
    ZX_ASSERT(IsDdiCoveredByThisRegister(ddi_id));
    const uint32_t bit_index = ((ddi_id - i915_tgl::DdiId::DDI_TC_1) & 0x1) * 4;
    return hwreg::BitfieldRef<const uint32_t>(reg_value_ptr(), bit_index + 3, bit_index).get();
  }

  // Setter of `connector_1_display_port_main_link_lane_{0,1,2,3}_enabled` and
  // `connector_0_display_port_main_link_lane_{0,1,2,3}_enabled` fields above
  // based on `ddi_id`.
  //
  // Callers must make sure they write to the correct FIA register.
  SelfType& set_enabled_display_port_main_link_lane_bits(i915_tgl::DdiId ddi_id, uint32_t bits) {
    ZX_ASSERT(IsDdiCoveredByThisRegister(ddi_id));
    if (IsSupportedDisplayPortLaneConfig(bits)) {
      const uint32_t lane0_enabled_bit_index = ((ddi_id - i915_tgl::DdiId::DDI_TC_1) & 0x1) * 4;
      hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), lane0_enabled_bit_index + 3,
                                   lane0_enabled_bit_index)
          .set(bits);
      return *this;
    }
    ZX_ASSERT_MSG(false, "invalid enabled_main_links_mask: 0x%x", bits);
  }

  static auto GetForDdi(i915_tgl::DdiId ddi_id) {
    ZX_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_TC_1);
    ZX_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_TC_6);
    const uint32_t fia_index = (ddi_id - i915_tgl::DdiId::DDI_TC_1) >> 1;
    return hwreg::RegisterAddr<SelfType>(kFiaOffsets[fia_index]);
  }

 private:
  static bool IsSupportedDisplayPortLaneConfig(uint32_t int_value) {
    switch (int_value) {
      case 0b0001:
      case 0b0011:
      case 0b1100:
      case 0b1111:
        return true;
      default:
        return false;
    }
  }

  bool IsDdiCoveredByThisRegister(i915_tgl::DdiId ddi_id) const {
    switch (reg_addr()) {
      case kFiaOffsets[0]:
        return ddi_id == i915_tgl::DdiId::DDI_TC_1 || ddi_id == i915_tgl::DdiId::DDI_TC_2;
      case kFiaOffsets[1]:
        return ddi_id == i915_tgl::DdiId::DDI_TC_3 || ddi_id == i915_tgl::DdiId::DDI_TC_4;
      case kFiaOffsets[2]:
        return ddi_id == i915_tgl::DdiId::DDI_TC_5 || ddi_id == i915_tgl::DdiId::DDI_TC_6;
      default:
        ZX_ASSERT_MSG(false, "Invalid register address 0x%x", reg_addr());
        return false;
    }
  }

  static constexpr uint32_t kFiaOffsets[] = {0x1638C0, 0x16E8C0, 0x16F8C0};
};

// PORT_TX_DFLEXDPSP (PORT_TX_DFLEXDPSP1)
// Dynamic FlexIO DP Scratch Pad for Type-C Connectors
//
// The connector ID here is the logical number for each FIA. Type-C port to FIA
// connector ID mapping is available at:
// Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0 "TypeC Programming" > "Port
// Mapping" table, Page 400.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 2, Pages 919-922.
class DynamicFlexIoScratchPad : public hwreg::RegisterBase<DynamicFlexIoScratchPad, uint32_t> {
 public:
  // This indicates whether a display is connected to the Type-C connector and
  // the type (DP-Alt on Type-C, or Thunderbolt) of the display.
  //
  // See enum class `TypeCLiveState` for valid values.
  //
  // Drivers can use the helper method `type_c_live_state` to get Type-C state
  // for a given DDI.
  //
  // The register has bits 0-7 representing the states for connector 0 to 7.
  // Since on Tiger Lake each FIA only connects to two connectors, we only
  // define the bits for connector 0 and 1 in this class.
  DEF_FIELD(15, 13, type_c_live_state_connector_1);

  // True if the IOM (Type C) firmware version supports MFD.
  //
  // If this bit is false, the IOM (Type C subsystem microcontroller) firmware
  // is too old to support MFD. This configuration is not supported by our
  // driver, as we assume MFD is always supported when configuring the Type-C
  // clock.
  //
  // The MFD acronym is not explained in Intel's documentation, but it probably
  // stands for Multi-functional display (simultaneous DisplayPort and USB
  // Enhanced SuperSpeed) over USB Type-C, as described in VESA DisplayPort Alt
  // Mode Standard Version 1.0b, Section 4.1 "Scenario 1 USB Type-C Cable".
  DEF_BIT(12, firmware_supports_mfd);

  // Firmware writes to the bits to indicate the PHY lane assignment for
  // display. Each bit correspond to a Type-C PHY lane (0-3).
  //
  // Drivers can use the helper method `display_port_tx_lane_assignment` to get
  // Type-C transmitter lane assignment for a given DDI, or use
  // `display_port_assigned_tx_lane_count` to count lanes assigned for DDI.
  //
  // The register has bits 0-7 representing the states for connector 0 to 7.
  // Since on Tiger Lake each FIA only connects to two connectors, we only
  // define the bits for connector 0 and 1 in this class.
  DEF_FIELD(11, 8, display_port_tx_lane_assignment_bits_connector_1);

  // Same as `type_c_live_state_connector_1` but for Connector 0.
  //
  // Drivers can use the helper method `type_c_live_state` to get Type-C state
  // for a given DDI.
  DEF_FIELD(7, 5, type_c_live_state_connector_0);

  // True if the FIA (Flexi IO Adapter) is modular.
  //
  // If this bit is false for the FIA1 register instance, the display engine has
  // one monolithic FIA that houses all connections (for example, Ice Lake).
  // The driver must not access the register instances for other FIAs.
  //
  // On Tiger Lake, this bit must be set true by the firmware, because Tiger
  // Lake display engines always have modular FIAs.
  //
  // If this bit is true for the FIA1 register instance, the display engine has
  // multiple modular FIAs, and each FIA instance hosts two Type C connections.
  DEF_BIT(4, is_modular_flexi_io_adapter);

  // Same as `tx_lane_assignment_bits_connector_1` but for Connector 0.
  //
  // Drivers can use the helper method `display_port_tx_lane_assignment` to get
  // Type-C transmitter lane assignment for a given DDI, or use
  // `display_port_assigned_tx_lane_count` to count lanes assigned for DDI.
  DEF_FIELD(3, 0, display_port_tx_lane_assignment_bits_connector_0);

  enum class TypeCLiveState : uint32_t {
    kNoHotplugDisplay = 0b000,
    kTypeCHotplugDisplay = 0b001,
    kThunderboltHotplugDisplay = 0b010,
    kInvalid = 0b011,
  };

  // Get the Type-C connection live state of a given DDI.
  //
  // This reads `type_c_live_state_connector_0` or
  // `type_c_live_state_connector_1` field based on `ddi_id`.
  //
  // Callers must make sure they read from the correct FIA register.
  TypeCLiveState type_c_live_state(i915_tgl::DdiId ddi_id) const {
    ZX_ASSERT(IsDdiCoveredByThisRegister(ddi_id));
    // TODO(fxbug.dev/110198): Move the logic to calculate register bit index
    // from given `ddi_id` to a separate method.
    const uint32_t bit_index = ((ddi_id - i915_tgl::DdiId::DDI_TC_1) & 0x1) * 8 + 5;
    auto val = hwreg::BitfieldRef<const uint32_t>(reg_value_ptr(), bit_index + 2, bit_index).get();
    if (IsSupportedTypeCLiveState(val)) {
      return static_cast<TypeCLiveState>(val);
    }
    zxlogf(WARNING, "PORT_TX_DFLEXDPSP: Invalid type_c_live_state: 0x%x", val);
    return TypeCLiveState::kInvalid;
  }

  // Get the PHY lane assignment for display of a given DDI.
  //
  // This reads `display_port_tx_lane_assignment_bits_connector_0` or
  // `display_port_tx_lane_assignment_bits_connector_1` field based on `ddi_id`.
  //
  // Callers must make sure they read from the correct FIA register.
  uint32_t display_port_tx_lane_assignment(i915_tgl::DdiId ddi_id) const {
    ZX_ASSERT(IsDdiCoveredByThisRegister(ddi_id));
    const uint32_t bit_index = ((ddi_id - i915_tgl::DdiId::DDI_TC_1) & 0x1) * 8;
    return hwreg::BitfieldRef<const uint32_t>(reg_value_ptr(), bit_index + 3, bit_index).get();
  }

  // A helper method to count number of lanes for display of a given DDI.
  //
  // This reads `display_port_tx_lane_assignment_bits_connector_0` or
  // `display_port_tx_lane_assignment_bits_connector_1` field based on `ddi_id`
  // and counts number of ones in the bitmap.
  //
  // Callers must make sure they read from the correct FIA register.
  size_t display_port_assigned_tx_lane_count(i915_tgl::DdiId ddi_id) const {
    auto assignment = display_port_tx_lane_assignment(ddi_id);
    return cpp20::popcount(assignment);
  }

  static auto GetForDdi(i915_tgl::DdiId ddi_id) {
    ZX_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_TC_1);
    ZX_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_TC_6);
    // TODO(fxbug.dev/110198): Move the logic to calculate FIA field index
    // from given `ddi_id` to a separate method.
    const uint32_t fia_index = (ddi_id - i915_tgl::DdiId::DDI_TC_1) >> 1;
    return hwreg::RegisterAddr<SelfType>(kFiaOffsets[fia_index]);
  }

 private:
  static bool IsSupportedTypeCLiveState(uint32_t int_value) {
    switch (int_value) {
      case 0b000:
      case 0b001:
      case 0b010:
      case 0b011:
        return true;
      default:
        return false;
    }
  }

  bool IsDdiCoveredByThisRegister(i915_tgl::DdiId ddi_id) const {
    switch (reg_addr()) {
      case kFiaOffsets[0]:
        return ddi_id == i915_tgl::DdiId::DDI_TC_1 || ddi_id == i915_tgl::DdiId::DDI_TC_2;
      case kFiaOffsets[1]:
        return ddi_id == i915_tgl::DdiId::DDI_TC_3 || ddi_id == i915_tgl::DdiId::DDI_TC_4;
      case kFiaOffsets[2]:
        return ddi_id == i915_tgl::DdiId::DDI_TC_5 || ddi_id == i915_tgl::DdiId::DDI_TC_6;
      default:
        ZX_ASSERT_MSG(false, "Invalid register address 0x%x", reg_addr());
        return false;
    }
  }

  static constexpr uint32_t kFiaOffsets[] = {0x1638A0, 0x16E8A0, 0x16F8A0};
};

// PORT_TX_DFLEXPA1
// Dynamic FlexIO Pin Assignment #1 (Connector 0-7)
//
// FIA arranges the 4 DisplayPort lanes in Type-C connector based on 6 possible
// arrangements called pin assignments A-F in VESA DisplayPort Alt Mode on USB
// Type-C Standard.
//
// This register is used by FIA to govern the pin assignment for each Type-C
// connector.
//
// The connector ID here is the logical number for each FIA. Type-C port to FIA
// connector ID mapping is available at:
// Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0 "TypeC Programming" > "Port
// Mapping" table, Page 400.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 2, Pages 925-926.
class DynamicFlexIoDisplayPortPinAssignment
    : public hwreg::RegisterBase<DynamicFlexIoDisplayPortPinAssignment, uint32_t> {
 public:
  // DisplayPort pin assignment for Type-C connector 1 (DPPATC1).
  // See enum class `PinAssignment` for bit definitions.
  //
  // Drivers can use `pin_assignment_for_ddi` helpers to access pin assignment
  // bitmap for a given DDI.
  //
  // The register has these bit fields for Connector 0 to 7. Since on Tiger
  // Lake each FIA only connects to two connectors, we only define the bits for
  // connector 0 and 1.
  DEF_FIELD(7, 4, display_port_pin_assignment_connector_1);

  // DisplayPort pin assignment for Type-C connector 0 (DPPATC0).
  // See enum class `PinAssignment` for bit definitions.
  //
  // Drivers can use `pin_assignment_for_ddi` helpers to access pin assignment
  // bitmap for a given DDI.
  DEF_FIELD(3, 0, display_port_pin_assignment_connector_0);

  // Maps DisplayPort Alt Mode pin assignments to register values.
  //
  // The pin assignments are described in the VESA DisplayPort Alt Mode on USB
  // Type-C Standard Version 2.0, Sections 3.1 "Pin Assignment Overview" and 3.2
  // "USB-C DP Pin Assignments" pages 34-36.
  //
  // The pin assignment bit definitions are available at
  // Tiger Lake: IHD-OS-TGL-Vol 2c-12.21-Rev 2.0 Part 2, Page 926 and
  //             IHD-OS-TGL-Vol 12-12.21-Rev 2.0, "DKL_DP_MODE Programming",
  //             Pages 397-398.
  //
  // Note that the section "DKL_DP_MODE Programming" in Vol 12 has a table that
  // includes values for pin assignments A-F. However, the register reference in
  // Vol 2c only documents the values for pin assignments C-E. This is likely
  // because the DisplayPort Alt Mode Standard states that assignments A, B,
  // and F are deprecated.
  enum class PinAssignment : uint32_t {
    // Fixed/static DisplayPort or HDMI connection.
    kNone = 0b0000,

    // Deprecated, 4 DisplayPort lanes.
    kA = 0b0001,

    // Deprecated, 2 DisplayPort lanes and 1 USB SuperSpeed TX/RX pair.
    kB = 0b0010,

    // 4 DisplayPort lanes, for USB-C to USB-C cables.
    kC = 0b0011,

    // 2 DisplayPort lanes and 1 USB SuperSpeed TX/RX pair, for USB-C to USB-C
    // cables.
    kD = 0b0100,

    // 4 DisplayPort lanes, for USB-C to DisplayPort cables.
    kE = 0b0101,

    // Deprecated, 2 DisplayPort lanes and 1 USB SuperSpeed TX/RX pair.
    kF = 0b0110,
  };

  // Get the pin assignment for given DDI.
  //
  // Pin assignments are defined at `display_port_pin_assignment_connector_0`
  // and `display_port_pin_assignment_connector_1`.
  //
  // Callers must make sure they read from the correct FIA register.
  std::optional<PinAssignment> pin_assignment_for_ddi(i915_tgl::DdiId ddi_id) const {
    ZX_ASSERT(IsDdiCoveredByThisRegister(ddi_id));
    const uint32_t bit_index = ((ddi_id - i915_tgl::DdiId::DDI_TC_1) & 0x1) * 4;
    auto raw_pin_assignment =
        hwreg::BitfieldRef<const uint32_t>(reg_value_ptr(), bit_index + 3, bit_index).get();
    if (IsValidPinAssignment(raw_pin_assignment)) {
      return static_cast<PinAssignment>(raw_pin_assignment);
    }
    zxlogf(WARNING, "PORT_TX_DFLEXPA1: Invalid pin assignment value for DDI %d: 0x%x", ddi_id,
           raw_pin_assignment);
    return std::nullopt;
  }

  static auto GetForDdi(i915_tgl::DdiId ddi_id) {
    ZX_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_TC_1);
    ZX_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_TC_6);
    const uint32_t fia_index = (ddi_id - i915_tgl::DdiId::DDI_TC_1) >> 1;
    return hwreg::RegisterAddr<SelfType>(kFiaOffsets[fia_index]);
  }

 private:
  static bool IsValidPinAssignment(uint32_t raw_pin_assignment) {
    switch (raw_pin_assignment) {
      case 0b0000:
      case 0b0001:
      case 0b0010:
      case 0b0011:
      case 0b0100:
      case 0b0101:
      case 0b0110:
        return true;
      default:
        return false;
    }
  }

  bool IsDdiCoveredByThisRegister(i915_tgl::DdiId ddi_id) const {
    switch (reg_addr()) {
      case kFiaOffsets[0]:
        return ddi_id == i915_tgl::DdiId::DDI_TC_1 || ddi_id == i915_tgl::DdiId::DDI_TC_2;
      case kFiaOffsets[1]:
        return ddi_id == i915_tgl::DdiId::DDI_TC_3 || ddi_id == i915_tgl::DdiId::DDI_TC_4;
      case kFiaOffsets[2]:
        return ddi_id == i915_tgl::DdiId::DDI_TC_5 || ddi_id == i915_tgl::DdiId::DDI_TC_6;
      default:
        ZX_ASSERT_MSG(false, "Invalid register address 0x%x", reg_addr());
        return false;
    }
  }

  static constexpr uint32_t kFiaOffsets[] = {0x163880, 0x16E880, 0x16F880};
};

// PORT_TX_DFLEXDPCSSS
// Dynamic FlexIo DisplayPort Controller Safe State Settings for Type-C
// Connectors (?)
//
// Display software (driver) uses this register to communicate with SOC micro-
// controller to enable / disable the safe mode of display controller.
//
// The connector ID here is the logical number for each FIA. Type-C port to FIA
// connector ID mapping is available at:
// Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0 "TypeC Programming" > "Port
// Mapping" table, Page 400.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 2, Pages 911-912.
class DynamicFlexIoDisplayPortControllerSafeStateSettings
    : public hwreg::RegisterBase<DynamicFlexIoDisplayPortControllerSafeStateSettings, uint32_t> {
 public:
  // If true, the Type C connector 1's DisplayPort PHY is not in a safe state.
  //
  // This field is also called DPPMSTC1 (DisplayPort Phy Mode State for
  // Connector 1) in Intel's documentation.
  //
  // Drivers can use `set_safe_mode_disabled_for_ddi` helpers to set safe mode
  // status for a given DDI.
  //
  // The register has these bit fields for Connector 0 to 7. Since on Tiger
  // Lake each FIA only connects to two connectors, we only define the bits for
  // connector 0 and 1.
  DEF_BIT(1, display_port_safe_mode_disabled_connector_1);

  // Similar to `display_port_safe_mode_disabled_connector_1` but for Type-C
  // Connector 0.
  //
  // This field is also called DPPMSTC0 (DisplayPort Phy Mode State for
  // Connector 0) in Intel's documentation.
  DEF_BIT(0, display_port_safe_mode_disabled_connector_0);

  // Disable / enable the PHY safe mode for given DDI.
  //
  // This helper method sets corresponding
  // `display_port_safe_mode_disabled_connector_0` or
  // `display_port_safe_mode_disabled_connector_1` based on `ddi_id` argument.
  //
  // Callers must make sure they write to the correct FIA register.
  SelfType& set_safe_mode_disabled_for_ddi(i915_tgl::DdiId ddi_id, bool disabled) {
    ZX_ASSERT(IsDdiCoveredByThisRegister(ddi_id));
    const uint32_t bit_index = (ddi_id - i915_tgl::DdiId::DDI_TC_1) & 0x1;
    hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit_index, bit_index).set(disabled);
    return *this;
  }

  static auto GetForDdi(i915_tgl::DdiId ddi_id) {
    ZX_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_TC_1);
    ZX_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_TC_6);
    const uint32_t fia_index = (ddi_id - i915_tgl::DdiId::DDI_TC_1) >> 1;
    return hwreg::RegisterAddr<SelfType>(kFiaOffsets[fia_index]);
  }

 private:
  bool IsDdiCoveredByThisRegister(i915_tgl::DdiId ddi_id) const {
    switch (reg_addr()) {
      case kFiaOffsets[0]:
        return ddi_id == i915_tgl::DdiId::DDI_TC_1 || ddi_id == i915_tgl::DdiId::DDI_TC_2;
      case kFiaOffsets[1]:
        return ddi_id == i915_tgl::DdiId::DDI_TC_3 || ddi_id == i915_tgl::DdiId::DDI_TC_4;
      case kFiaOffsets[2]:
        return ddi_id == i915_tgl::DdiId::DDI_TC_5 || ddi_id == i915_tgl::DdiId::DDI_TC_6;
      default:
        ZX_ASSERT_MSG(false, "Invalid register address 0x%x", reg_addr());
        return false;
    }
  }

  static constexpr uint32_t kFiaOffsets[] = {0x163894, 0x16E894, 0x16F894};
};

// PORT_TX_DFLEXDPPMS
// Dynamic FlexIO DisplayPort PHY Safe Mode Status for Type-C Connectors
//
// Firmware writes to this register to tell display driver whether the Type-C
// PHY is ready for a given connector (i.e. SOC microcontroller has switched
// the lane into DP mode).
//
// The connector ID here is the logical number for each FIA. Type-C port to FIA
// connector ID mapping is available at:
// Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0 "TypeC Programming" > "Port
// Mapping" table, Page 400.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 2, Pages 916-917.
class DynamicFlexIoDisplayPortPhyModeStatus
    : public hwreg::RegisterBase<DynamicFlexIoDisplayPortPhyModeStatus, uint32_t> {
 public:
  // Indicates the PHY readiness for Connector 1 (DFLEXDPPMS.DPPMSTC1).
  //
  // Drivers can use `phy_is_ready_for_ddi` helpers to get PHY status for a
  // given DDI.
  //
  // The register has these bit fields for Connector 0 to 15. Since on Tiger
  // Lake each FIA only connects to two connectors, we only define the bits for
  // connector 0 and 1.
  DEF_BIT(1, display_port_phy_is_ready_connector_1);

  // Indicates the PHY readiness for Connector 0 (DFLEXDPPMS.DPPMSTC0).
  //
  // Drivers can use `phy_is_ready_for_ddi` helpers to get PHY status for a
  // given DDI.
  DEF_BIT(0, display_port_phy_is_ready_connector_0);

  // Whether the PHY is ready to use for DisplayPort transmission.
  //
  // This helper method reads `display_port_phy_is_ready_connector_0` or
  // `display_port_phy_is_ready_connector_1` bit based on given `ddi_id`.
  //
  // Callers must make sure they read from the correct FIA register.
  bool phy_is_ready_for_ddi(i915_tgl::DdiId ddi_id) {
    ZX_ASSERT(IsDdiCoveredByThisRegister(ddi_id));
    const uint32_t bit_index = (ddi_id - i915_tgl::DdiId::DDI_TC_1) & 0x1;
    return hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit_index, bit_index).get();
  }

  static auto GetForDdi(i915_tgl::DdiId ddi_id) {
    ZX_DEBUG_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_TC_1);
    ZX_DEBUG_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_TC_6);
    const uint32_t fia_index = (ddi_id - i915_tgl::DdiId::DDI_TC_1) >> 1;
    return hwreg::RegisterAddr<SelfType>(kFiaOffsets[fia_index]);
  }

 private:
  bool IsDdiCoveredByThisRegister(i915_tgl::DdiId ddi_id) const {
    switch (reg_addr()) {
      case kFiaOffsets[0]:
        return ddi_id == i915_tgl::DdiId::DDI_TC_1 || ddi_id == i915_tgl::DdiId::DDI_TC_2;
      case kFiaOffsets[1]:
        return ddi_id == i915_tgl::DdiId::DDI_TC_3 || ddi_id == i915_tgl::DdiId::DDI_TC_4;
      case kFiaOffsets[2]:
        return ddi_id == i915_tgl::DdiId::DDI_TC_5 || ddi_id == i915_tgl::DdiId::DDI_TC_6;
      default:
        ZX_ASSERT_MSG(false, "Invalid register address 0x%x", reg_addr());
        return false;
    }
  }

  static constexpr uint32_t kFiaOffsets[] = {0x163890, 0x16E890, 0x16F890};
};

// ============================================================================
//                        Dekel (DKL) PHY/PLL registers
// ============================================================================
// TODO(fxbug.dev/110198): Consider moving these register definitions into a
// separated file.
//
// Registers below controls Type-C port PHY (i.e. Dekel PHY), including clock,
// DisplayPort output, PHY uC (microcontroller) state, etc.
//
// Each Type-C PHY has more than 4KB of register space but the addressing space
// is only 4KB. In order to access DKL registers, driver must set the upper 2
// address bits to corresponding bits in `HIP_INDEX_REG*` register before
// accessing the MMIO address using "PHY base address + the lower 10 bits of
// register internal address".
//
// All Dekel PHY / PLL registers are defined as "DekelRegisterBase". It writes
// the MMIO index to HIP_INDEX_REG* registers before accessing the actual PHY
// register on `ReadFrom()` / `WriteTo()`.

// HIP_INDEX_REG0
//
// This register provides index window for the following MMIO ranges:
// - (Port Type C 1): 0x168000 - 0x168FFF
// - (Port Type C 2): 0x169000 - 0x169FFF
// - (Port Type C 3): 0x16A000 - 0x16AFFF
// - (Port Type C 4): 0x16B000 - 0x16BFFF
//
// On Tiger Lake, the port number and PHY base address register / field mapping
// is available at: IHD-OS-TGL-Vol 12-1.22-Rev 2.0, Page 415.
//
class HipIndexReg0 : public hwreg::RegisterBase<HipIndexReg0, uint32_t> {
 public:
  // HIP_16B_Index
  //
  // Drivers can access DDI-specific index value using `set_hip_index_for_ddi`.
  DEF_FIELD(27, 24, hip_index_type_c_4);

  // HIP_16A_Index
  //
  // Drivers can access DDI-specific index value using `set_hip_index_for_ddi`.
  DEF_FIELD(19, 16, hip_index_type_c_3);

  // HIP_169_Index
  //
  // Drivers can access DDI-specific index value using `set_hip_index_for_ddi`.
  DEF_FIELD(11, 8, hip_index_type_c_2);

  // HIP_168_Index
  //
  // Drivers can access DDI-specific index value using `set_hip_index_for_ddi`.
  DEF_FIELD(3, 0, hip_index_type_c_1);

  // Helper method to write index value for given DDI.
  //
  // This writes to corresponding field `hip_index_type_c_1`,
  // `hip_index_type_c_2`, `hip_index_type_c_3` or `hip_index_type_c_4` based on
  // given `ddi_id`.
  SelfType& set_hip_index_for_ddi(i915_tgl::DdiId ddi_id, uint32_t hip_index) {
    ZX_ASSERT_MSG((hip_index & (~0b1111)) == 0,
                  "hip_index (0x%x) invalid: it has more than 4 bits.", hip_index);
    ZX_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_TC_1);
    ZX_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_TC_4);

    const uint32_t bit_low = (ddi_id - i915_tgl::DdiId::DDI_TC_1) * 8;
    const uint32_t bit_high = bit_low + 3;
    hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit_high, bit_low).set(hip_index);

    return *static_cast<SelfType*>(this);
  }

  static auto Get() { return hwreg::RegisterAddr<HipIndexReg0>(0x1010a0); }
};

// HIP_INDEX_REG1
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 1, Page 1249
class HipIndexReg1 : public hwreg::RegisterBase<HipIndexReg1, uint32_t> {
 public:
  // HIP_16D_Index
  //
  // Drivers can access DDI-specific index value using `set_hip_index_for_ddi`.
  //
  // This register also has HIP index for MMIO range 16E000 and 16F000.
  // Since they don't map to any Type-C port on Tiger Lake, we omit these
  // fields in the register class definition.
  DEF_FIELD(11, 8, hip_index_type_c_6);

  // HIP_16C_Index
  //
  // Drivers can access DDI-specific index value using `set_hip_index_for_ddi`.
  DEF_FIELD(3, 0, hip_index_type_c_5);

  // Helper method to write index value for given DDI.
  //
  // This writes to corresponding field `hip_index_type_c_5` or
  // `hip_index_type_c_6` based on given `ddi_id`.
  SelfType& set_hip_index_for_ddi(i915_tgl::DdiId ddi_id, uint32_t hip_index) {
    ZX_ASSERT_MSG((hip_index & (~0b1111)) == 0,
                  "hip_index (0x%x) invalid: it has more than 4 bits.", hip_index);
    ZX_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_TC_5);
    ZX_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_TC_6);

    const uint32_t bit_low = (ddi_id - i915_tgl::DdiId::DDI_TC_5) * 8;
    const uint32_t bit_high = bit_low + 3;
    hwreg::BitfieldRef<uint32_t>(reg_value_ptr(), bit_high, bit_low).set(hip_index);

    return *static_cast<SelfType*>(this);
  }

  static auto Get() { return hwreg::RegisterAddr<HipIndexReg1>(0x1010a4); }
};

template <typename T>
void WriteHipIndex(T* reg_io, i915_tgl::DdiId ddi_id, uint32_t hip_index) {
  switch (ddi_id) {
    case i915_tgl::DdiId::DDI_TC_1:
    case i915_tgl::DdiId::DDI_TC_2:
    case i915_tgl::DdiId::DDI_TC_3:
    case i915_tgl::DdiId::DDI_TC_4: {
      auto hip_index_reg0 = HipIndexReg0::Get().ReadFrom(reg_io);
      hip_index_reg0.set_hip_index_for_ddi(ddi_id, hip_index).WriteTo(reg_io);
      break;
    }
    case i915_tgl::DdiId::DDI_TC_5:
    case i915_tgl::DdiId::DDI_TC_6: {
      auto hip_index_reg1 = HipIndexReg1::Get().ReadFrom(reg_io);
      hip_index_reg1.set_hip_index_for_ddi(ddi_id, hip_index).WriteTo(reg_io);
      break;
    }
    default:
      ZX_ASSERT_MSG(false, "WriteHipIndex: Unsupported DDI %d", ddi_id);
  }
}

// Register base class for Dekel PHY / PLL registers.
// It writes the HIP index to corresponding HIP_INDEX_* register before reading
// from or writing to the MMIO register.
//
// The Dekel PHY register access logic is available at:
// Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0 "Dekel PHY Register Access" pages
//             414-416
template <class DerivedType, class IntType, class PrinterState = void>
class DekelRegisterBase : public ::hwreg::RegisterBase<DerivedType, IntType, PrinterState> {
 public:
  using ParentType = typename ::hwreg::RegisterBase<DerivedType, IntType, PrinterState>;
  using SelfType = DerivedType;
  using ValueType = IntType;
  using PrinterEnabled = std::is_same<PrinterState, hwreg::EnablePrinter>;

  template <typename T>
  SelfType& ReadFrom(T* reg_io) {
    const uint32_t mmio_index = phy_internal_address_ >> 12;
    WriteHipIndex(reg_io, ddi_id_, mmio_index);
    ParentType::ReadFrom(reg_io);
    return *static_cast<SelfType*>(this);
  }

  template <typename T>
  SelfType& WriteTo(T* reg_io) {
    const uint32_t mmio_index = phy_internal_address_ >> 12;
    WriteHipIndex(reg_io, ddi_id_, mmio_index);
    ParentType::WriteTo(reg_io);
    return *static_cast<SelfType*>(this);
  }

  SelfType& set_ddi(i915_tgl::DdiId ddi_id) {
    ddi_id_ = ddi_id;
    return *static_cast<SelfType*>(this);
  }

  SelfType& set_phy_internal_address(uint32_t phy_internal_address) {
    phy_internal_address_ = phy_internal_address;
    ParentType::set_reg_addr(PhyBaseAddress(ddi_id_) + (phy_internal_address & 0xfff));
    return *static_cast<SelfType*>(this);
  }

  // The base address is not complete on Tiger Lake documentation. The addresses
  // documented in Lakefield PRM are complete and matches Tiger Lake
  // counterparts. We have verified the Lakefield base addresses can work on
  // Tiger Lake as well.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0 "Dekel PHY Register Access",
  //             pages 414-416
  // Lakefield: IHD-OS-LKF-Vol 12-4.21 "Dekel PHY Programming" pages 319-321
  static uint32_t PhyBaseAddress(i915_tgl::DdiId ddi_id) {
    ZX_ASSERT(ddi_id >= i915_tgl::DdiId::DDI_TC_1);
    ZX_ASSERT(ddi_id <= i915_tgl::DdiId::DDI_TC_6);
    return 0x168000 + (ddi_id - i915_tgl::DdiId::DDI_TC_1) * 0x1000;
  }

 private:
  using ParentType::reg_addr;
  using ParentType::set_reg_addr;

  i915_tgl::DdiId ddi_id_;
  uint32_t phy_internal_address_;
};

template <class RegType>
class DekelRegisterAddr {
 public:
  DekelRegisterAddr(i915_tgl::DdiId ddi_id, uint32_t phy_internal_address)
      : ddi_id_(ddi_id), phy_internal_address_(phy_internal_address) {}

  static_assert(
      std::is_base_of<DekelRegisterBase<RegType, typename RegType::ValueType>, RegType>::value ||
          std::is_base_of<
              DekelRegisterBase<RegType, typename RegType::ValueType, hwreg::EnablePrinter>,
              RegType>::value,
      "Parameter of DekelRegisterAddr<> should derive from DekelRegisterBase");

  // Instantiate a DekelRegisterBase using the value of the register read from
  // MMIO.
  template <typename T>
  RegType ReadFrom(T* reg_io) {
    RegType reg;
    reg.set_ddi(ddi_id_).set_phy_internal_address(phy_internal_address_);
    reg.ReadFrom(reg_io);
    return reg;
  }

  // Instantiate a DekelRegisterBase using the given value for the register.
  RegType FromValue(typename RegType::ValueType value) {
    RegType reg;
    reg.set_ddi(ddi_id_).set_phy_internal_address(phy_internal_address_);
    reg.set_reg_value(value);
    return reg;
  }

 private:
  const i915_tgl::DdiId ddi_id_;
  const uint32_t phy_internal_address_;
};

// This is used to define opaque registers that have no field definition.
//
// Drivers can use:
// ```
// using DekelRegisterName = DekelOpaqueRegister<PhyInternalAddress>;
// ```
// to define an opqaue register when they don't need to modify specific fields.
template <uint32_t PhyInternalAddr>
class DekelOpaqueRegister
    : public DekelRegisterBase<DekelOpaqueRegister<PhyInternalAddr>, uint32_t> {
 public:
  using SelfType = DekelOpaqueRegister<PhyInternalAddr>;

  static auto GetForDdi(i915_tgl::DdiId ddi_id) {
    return DekelRegisterAddr<SelfType>(ddi_id, PhyInternalAddr);
  }
};

// DKL_PLL_DIV0
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 1, Pages 460-461
class DekelPllDivisor0 : public DekelRegisterBase<DekelPllDivisor0, uint32_t> {
 public:
  // Field `i_fbprediv_3_0`. Predivider ratio.
  // Valid values: 2 means /2, 4 means /4.
  // All the other values are reserved.
  DEF_FIELD(11, 8, feedback_predivider_ratio);

  // Field `i_fbdiv_intgr`.
  // Integer part of feedback divider post division.
  // The fractional part is at `i_fbdiv_frac_21_0` field of `DKL_BIAS` register.
  DEF_FIELD(7, 0, feedback_divider_integer_part);

  // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev 2.0 "PHY Registers" pages 415-416
  static auto GetForDdi(i915_tgl::DdiId ddi_id) {
    return DekelRegisterAddr<DekelPllDivisor0>(ddi_id, 0x2200);
  }
};

// DKL_PLL_DIV1
//
// PLL DIV1 config register.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 1, Pages 462-463
using DekelPllDivisor1 = DekelOpaqueRegister<0x2204>;

// DKL_PLL_FRAC_LOCK
//
// PLL FRAC_LOCK config register.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 1, Pages 464-465
using DekelPllFractionalLock = DekelOpaqueRegister<0x220C>;

// DKL_PLL_LF
//
// PLL LF config register.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 1, Pages 466-467
using DekelPllLf = DekelOpaqueRegister<0x2208>;

// DKL_SSC
//
// PLL SSC config register.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 1, Pages 473-474
using DekelPllSsc = DekelOpaqueRegister<0x2210>;

// DKL_BIAS
//
// PLL BIAS config register.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 1, Page 444
class DekelPllBias : public DekelRegisterBase<DekelPllBias, uint32_t> {
 public:
  // Field `i_fracnen_h`. Enables fractional modulator.
  DEF_BIT(30, fractional_modulator_enabled);

  // This merges `i_fbdiv_frac_21_16`, `i_fbdiv_frac_15_8` and
  // `i_fbdiv_frac_7_0`. It's the fractional part of the feedback divider.
  DEF_FIELD(29, 8, feedback_divider_fractional_part_22_bits);

  // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev 2.0 "PHY Registers" pages 415-416
  static auto GetForDdi(i915_tgl::DdiId ddi_id) {
    return DekelRegisterAddr<DekelPllBias>(ddi_id, 0x2214);
  }
};

// DKL_TDC_COLDST_BIAS
//
// PLL TDC_COLDST_BIAS config register.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 1, Page 475
using DekelPllTdcColdstBias = DekelOpaqueRegister<0x2218>;

// DKL_REFCLKIN_CTL
//
// PLL reference clock input control register.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 1, Page 472
using DekelPllReferenceClockInputControl = DekelOpaqueRegister<0x212C>;

// DKL_CMN_DIG_PLL_MISC
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 1, Page 453
//
// The register internal address is documented at
// Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev 2.0, Page 189-190
using DekelCommonConfigDigitalPllMisc = DekelOpaqueRegister<0x203C>;

// DKL_CMN_ANA_DWORD28
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 1, Page 451
//
// The register internal address is documented at
// Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev 2.0, Page 189-190
using DekelCommonConfigAnalogDword28 = DekelOpaqueRegister<0x2130>;

// DKL_CLKTOP2_HSCLKCTL
//
// PLL High-speed clock control register.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 1, Pages 447-450
class DekelPllClktop2HighSpeedClockControl
    : public DekelRegisterBase<DekelPllClktop2HighSpeedClockControl, uint32_t> {
 public:
  // Valid values of field `high_speed_divider_ratio_selection` (defined below).
  enum class HighSpeedDividerRatioSelection : uint32_t {
    k2 = 0b00,
    k3 = 0b01,
    k5 = 0b10,
    k7 = 0b11,
  };

  // Field `od_clktop2_hsdiv_divratio`. Divider ratio selection for high speed
  // divider (DIV1).
  //
  // Drivers can use helper method `high_speed_divider_ratio()` to get the
  // divider ratio in standard integer format.
  DEF_ENUM_FIELD(HighSpeedDividerRatioSelection, 13, 12, high_speed_divider_ratio_selection);

  // Field `od_clktop2_dsdiv_divratio`. Divider radio settings for programmable
  // divider (DIV2).
  //
  // Allowed values are 0 (No division), and from 1 (divide by 1; no division)
  // to 10 (divide by 10).
  //
  // Drivers can use helper method `programmable_divider_ratio()` to get the
  // divider ratio in standard integer format.
  DEF_FIELD(11, 8, programmable_divider_ratio_selection);

  // Helper method to get actual high speed divider ratio (DIV1).
  //
  // This reads `high_speed_divider_ratio_selection` field and translates the
  // value into standard integer format.
  uint32_t high_speed_divider_ratio() const {
    switch (high_speed_divider_ratio_selection()) {
      case HighSpeedDividerRatioSelection::k2:
        return 2;
      case HighSpeedDividerRatioSelection::k3:
        return 3;
      case HighSpeedDividerRatioSelection::k5:
        return 5;
      case HighSpeedDividerRatioSelection::k7:
        return 7;
    }
  }

  // Helper method to get actual programmable divider ratio (DIV2).
  //
  // This reads `programmable_divider_ratio` field and translates the
  // value into standard integer format.
  uint32_t programmable_divider_ratio() const {
    if (programmable_divider_ratio_selection() > 10) {
      zxlogf(WARNING, "DKL_CLKTOP2_HSCLKCTL: Invalid programmable_divider_ratio selection: %u",
             programmable_divider_ratio_selection());
    }
    if (programmable_divider_ratio_selection() == 0) {
      // To avoid division by zero.
      return 1;
    }
    return programmable_divider_ratio_selection();
  }

  static auto GetForDdi(i915_tgl::DdiId ddi_id) {
    // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev 2.0 "PHY Registers" pages 415-416
    return DekelRegisterAddr<DekelPllClktop2HighSpeedClockControl>(ddi_id, 0x20D4);
  }
};

// DKL_CLKTOP2_CORECLKCTL1
//
// PLL Core clock control register.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 1, Pages 445-446
using DekelPllClktop2CoreClockControl1 = DekelOpaqueRegister<0x20D8>;

// DKL_CMN_UC_DW27
//
// Microcontroller (uC) config register.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 1, Pages 454-457
class DekelCommonConfigMicroControllerDword27
    : public DekelRegisterBase<DekelCommonConfigMicroControllerDword27, uint32_t> {
 public:
  // Indicates whether the PHY uC firmware is ready in uC mode.
  DEF_BIT(15, microcontroller_firmware_is_ready);

  static auto GetForDdi(i915_tgl::DdiId ddi_id) {
    return DekelRegisterAddr<DekelCommonConfigMicroControllerDword27>(ddi_id, 0x236C);
  }
};

// DKL_DP_MODE
//
// DisplayPort mode config. Each lane has its own DKL_DP_MODE register
// controlling its PHY transmitters.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 1, Pages 458-459
class DekelDisplayPortMode : public DekelRegisterBase<DekelDisplayPortMode, uint32_t> {
 public:
  // Field `cfg_dp_x2_mode`. Indicates x2 mode for DP.
  //
  // `x2_mode` and `x1_mode` bits determine the active PHY transmitter used
  // by the lane.
  //
  // On Tiger Lake, per IHD-OS-TGL-Vol 12-1.22-Rev 2.0,
  // - When `x2_mode` == 0 and `x1_mode` == 0, only TX1 is active.
  // - When `x2_mode` == 0 and `x1_mode` == 1, only TX2 is active.
  // - When `x2_mode` == 1`, both TX1 and TX2 are active.
  DEF_BIT(7, x2_mode);

  // Field `cfg_dp_x1_mode`. Indicates x1 mode for DP.
  //
  // See above `x2_mode` field documentation for how to decode the field.
  DEF_BIT(6, x1_mode);

  static auto GetForLaneDdi(uint32_t lane, i915_tgl::DdiId ddi_id) {
    ZX_ASSERT(lane == 0 || lane == 1);
    // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev 2.0 "PHY Registers" pages 415-416
    const uint32_t phy_internal_address = lane == 0 ? 0x00A0 : 0x10A0;
    return DekelRegisterAddr<DekelDisplayPortMode>(ddi_id, phy_internal_address);
  }
};

// DKL_TX_DPCNTL0
// Dekel Transmitter DisplayPort Control Register #0 (?)
//
// Each lane has its own DKL_TX_DPCNTL0 register.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 1, Page 476
class DekelTransmitterDisplayPortControl0
    : public DekelRegisterBase<DekelTransmitterDisplayPortControl0, uint32_t> {
 public:
  // Preshoot level on voltage swing
  //
  // See IHD-OS-TGL-Vol 12-1.22-Rev 2.0, Pages 396-397 for valid values.
  DEF_FIELD(17, 13, preshoot_coefficient_transmitter_1);

  // De-emphasis level on voltage swing
  DEF_FIELD(12, 8, de_emphasis_coefficient_transmitter_1);

  // Voltage swing level
  //
  // See IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 1, Page 477 for level - voltage
  // mappings.
  DEF_FIELD(2, 0, voltage_swing_control_level_transmitter_1);

  static auto GetForLaneDdi(uint32_t lane, i915_tgl::DdiId ddi_id) {
    ZX_ASSERT(lane == 0 || lane == 1);
    // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev 2.0 "PHY Registers" pages 415-416
    const uint32_t phy_internal_address = lane == 0 ? 0x02C0 : 0x12C0;
    return DekelRegisterAddr<DekelTransmitterDisplayPortControl0>(ddi_id, phy_internal_address);
  }
};

// DKL_TX_DPCNTL1
// Dekel Transmitter DisplayPort Control Register #1 (?)
//
// Each lane has its own DKL_TX_DPCNTL1 register.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 1, Page 477
class DekelTransmitterDisplayPortControl1
    : public DekelRegisterBase<DekelTransmitterDisplayPortControl1, uint32_t> {
 public:
  // Preshoot level on voltage swing
  //
  // See IHD-OS-TGL-Vol 12-1.22-Rev 2.0, Pages 396-397 for valid values.
  DEF_FIELD(17, 13, preshoot_coefficient_transmitter_2);

  // De-emphasis level on voltage swing
  DEF_FIELD(12, 8, de_emphasis_coefficient_transmitter_2);

  // Voltage swing level
  //
  // See IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 1, Page 477 for level - voltage
  // mappings.
  DEF_FIELD(2, 0, voltage_swing_control_level_transmitter_2);

  static auto GetForLaneDdi(uint32_t lane, i915_tgl::DdiId ddi_id) {
    ZX_ASSERT(lane == 0 || lane == 1);
    // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev 2.0 "PHY Registers" pages 415-416
    const uint32_t phy_internal_address = lane == 0 ? 0x02C4 : 0x12C4;
    return DekelRegisterAddr<DekelTransmitterDisplayPortControl1>(ddi_id, phy_internal_address);
  }
};

// DKL_TX_DPCNTL2
// Dekel Transmitter DisplayPort Control Register #2 (?)
//
// Each lane has its own DKL_TX_DPCNTL2 register.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 1, Page 478
class DekelTransmitterDisplayPortControl2
    : public DekelRegisterBase<DekelTransmitterDisplayPortControl2, uint32_t> {
 public:
  // This needs to be set to 1 if Pipe width doesn't reflect the 20 bit mode.
  DEF_BIT(2, display_port_20bit_mode_supported);

  static auto GetForLaneDdi(uint32_t lane, i915_tgl::DdiId ddi_id) {
    ZX_ASSERT(lane == 0 || lane == 1);
    // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev 2.0 "PHY Registers" pages 415-416
    const uint32_t phy_internal_address = lane == 0 ? 0x02C8 : 0x12C8;
    return DekelRegisterAddr<DekelTransmitterDisplayPortControl2>(ddi_id, phy_internal_address);
  }
};

// DKL_TX_PMD_LANE_SUS
//
// Each lane has its own DKL_TX_PMD_LANE_SUS register.
//
// Driver should flush all register bits to 0 at the time display driver
// takes control of the PHY lane.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev 2.0 Part 1, Pages 482-483
class DekelTransmitterPmdLaneSus : public DekelRegisterBase<DekelTransmitterPmdLaneSus, uint32_t> {
 public:
  static auto GetForLaneDdi(uint32_t lane, i915_tgl::DdiId ddi_id) {
    ZX_ASSERT(lane == 0 || lane == 1);
    // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev 2.0 "PHY Registers" pages 415-416
    const uint32_t phy_internal_address = lane == 0 ? 0x0D00 : 0x1D00;
    return DekelRegisterAddr<DekelTransmitterDisplayPortControl2>(ddi_id, phy_internal_address);
  }
};

}  // namespace tgl_registers

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_TYPEC_H_
