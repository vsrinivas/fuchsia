// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/ddi-physical-layer.h"

#include <lib/fit/defer.h>
#include <lib/mmio/mmio-buffer.h>
#include <lib/zx/result.h>
#include <zircon/assert.h>

#include <fbl/string_printf.h>

#include "src/graphics/display/drivers/intel-i915-tgl/ddi-physical-layer-internal.h"
#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"
#include "src/graphics/display/drivers/intel-i915-tgl/intel-i915-tgl.h"
#include "src/graphics/display/drivers/intel-i915-tgl/poll-until.h"
#include "src/graphics/display/drivers/intel-i915-tgl/power-controller.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-typec.h"

namespace i915_tgl {

namespace {

const char* DdiTypeToString(DdiPhysicalLayer::DdiType type) {
  switch (type) {
    case DdiPhysicalLayer::DdiType::kCombo:
      return "COMBO";
    case DdiPhysicalLayer::DdiType::kTypeC:
      return "Type-C";
  }
}

const char* PortTypeToString(DdiPhysicalLayer::ConnectionType type) {
  switch (type) {
    case DdiPhysicalLayer::ConnectionType::kNone:
      return "None";
    case DdiPhysicalLayer::ConnectionType::kBuiltIn:
      return "Built In";
    case DdiPhysicalLayer::ConnectionType::kTypeCDisplayPortAltMode:
      return "Type-C DisplayPort Alt Mode";
    case DdiPhysicalLayer::ConnectionType::kTypeCThunderbolt:
      return "Type-C Thunderbolt Mode";
      break;
  }
}

}  // namespace

void DdiPhysicalLayer::AddRef() {
  ZX_DEBUG_ASSERT(IsEnabled());
  ++ref_count_;
  zxlogf(TRACE, "DdiPhysicalLayer: Reference count of DDI %d increased to %d", ddi_id(),
         ref_count_);
}

void DdiPhysicalLayer::Release() {
  ZX_DEBUG_ASSERT(ref_count_ > 0);
  if (--ref_count_ == 0) {
    zxlogf(TRACE, "DdiPhysicalLayer: Reference count of DDI %d decreased to %d", ddi_id(),
           ref_count_);
    if (!Disable()) {
      zxlogf(ERROR, "DdiPhysicalLayer: Failed to disable unused DDI %d", ddi_id());
    }
  }
}

fbl::String DdiPhysicalLayer::PhysicalLayerInfo::DebugString() const {
  return fbl::StringPrintf("PhysicalLayerInfo { type: %s, port: %s, max_allowed_dp_lane: %u }",
                           DdiTypeToString(ddi_type), PortTypeToString(connection_type),
                           max_allowed_dp_lane_count);
}

bool DdiSkylake::Enable() {
  if (enabled_) {
    zxlogf(WARNING, "DDI %d: Enable: PHY already enabled", ddi_id());
  }
  enabled_ = true;
  return true;
}

bool DdiSkylake::Disable() {
  if (!enabled_) {
    zxlogf(WARNING, "DDI %d: Disable: PHY already disabled", ddi_id());
  }
  enabled_ = false;
  return true;
}

DdiPhysicalLayer::PhysicalLayerInfo DdiSkylake::GetPhysicalLayerInfo() const {
  return {
      .ddi_type = DdiPhysicalLayer::DdiType::kCombo,
      .connection_type = DdiPhysicalLayer::ConnectionType::kBuiltIn,
      .max_allowed_dp_lane_count = 4u,
  };
}

bool ComboDdiTigerLake::Enable() {
  if (enabled_) {
    zxlogf(WARNING, "DDI %d: Enable: PHY already enabled", ddi_id());
  }
  enabled_ = true;
  return true;
}

bool ComboDdiTigerLake::Disable() {
  if (!enabled_) {
    zxlogf(WARNING, "DDI %d: Enable: PHY already disabled", ddi_id());
  }
  enabled_ = false;
  return true;
}

DdiPhysicalLayer::PhysicalLayerInfo ComboDdiTigerLake::GetPhysicalLayerInfo() const {
  return {
      .ddi_type = DdiPhysicalLayer::DdiType::kCombo,
      .connection_type = DdiPhysicalLayer::ConnectionType::kBuiltIn,
      .max_allowed_dp_lane_count = 4u,
  };
}

TypeCDdiTigerLake::TypeCDdiTigerLake(DdiId ddi_id, Power* power, fdf::MmioBuffer* mmio_space,
                                     bool is_static_port)
    : DdiPhysicalLayer(ddi_id),
      power_(power),
      mmio_space_(mmio_space),
      initialization_phase_(InitializationPhase::kUninitialized),
      is_static_port_(is_static_port),
      physical_layer_info_(DefaultPhysicalLayerInfo()) {
  ZX_ASSERT(power_);
  ZX_ASSERT(mmio_space_);
  ZX_ASSERT(ddi_id >= DdiId::DDI_TC_1);
  ZX_ASSERT(ddi_id <= DdiId::DDI_TC_6);
}

TypeCDdiTigerLake::~TypeCDdiTigerLake() {
  if (initialization_phase_ != InitializationPhase::kUninitialized) {
    zxlogf(WARNING, "DDI %d: not fully disabled on port teardown", ddi_id());
  }
}

bool TypeCDdiTigerLake::IsEnabled() const {
  return initialization_phase_ == InitializationPhase::kInitialized;
}

bool TypeCDdiTigerLake::IsHealthy() const {
  // All the other states indicate that the DDI PHY is not fully initialized
  // or not fully deinitialized and thus in a limbo state.
  return initialization_phase_ == InitializationPhase::kInitialized ||
         initialization_phase_ == InitializationPhase::kUninitialized;
}

DdiPhysicalLayer::PhysicalLayerInfo TypeCDdiTigerLake::ReadPhysicalLayerInfo() const {
  PhysicalLayerInfo physical_layer_info = {
      .ddi_type = DdiType::kTypeC,
  };

  auto dp_sp = tgl_registers::DynamicFlexIoScratchPad::GetForDdi(ddi_id()).ReadFrom(mmio_space_);
  auto type_c_live_state = dp_sp.type_c_live_state(ddi_id());
  switch (type_c_live_state) {
    using TypeCLiveState = tgl_registers::DynamicFlexIoScratchPad::TypeCLiveState;
    case TypeCLiveState::kNoHotplugDisplay:
      if (is_static_port_) {
        physical_layer_info.connection_type = ConnectionType::kBuiltIn;
        physical_layer_info.max_allowed_dp_lane_count = 4u;
      } else {
        physical_layer_info.connection_type = ConnectionType::kNone;
        physical_layer_info.max_allowed_dp_lane_count = 0u;
      }
      break;
    case TypeCLiveState::kTypeCHotplugDisplay:
      physical_layer_info.connection_type = ConnectionType::kTypeCDisplayPortAltMode;
      physical_layer_info.max_allowed_dp_lane_count =
          dp_sp.display_port_assigned_tx_lane_count(ddi_id());
      break;
    case TypeCLiveState::kThunderboltHotplugDisplay:
      physical_layer_info.connection_type = ConnectionType::kTypeCThunderbolt;
      physical_layer_info.max_allowed_dp_lane_count = 4u;
      break;
    default:
      ZX_ASSERT_MSG(false, "DDI %d: unsupported type C live state (0x%x)", ddi_id(),
                    type_c_live_state);
  }

  return physical_layer_info;
}

bool TypeCDdiTigerLake::AdvanceEnableFsm() {
  switch (initialization_phase_) {
    case InitializationPhase::kUninitialized:
      initialization_phase_ = InitializationPhase::kTypeCColdBlocked;
      return BlockTypeCColdPowerState();
    case InitializationPhase::kTypeCColdBlocked:
      initialization_phase_ = InitializationPhase::kSafeModeSet;
      if (!SetPhySafeModeDisabled(/*target_disabled=*/true)) {
        return false;
      }
      physical_layer_info_ = ReadPhysicalLayerInfo();
      return physical_layer_info_.connection_type != ConnectionType::kNone;
    case InitializationPhase::kSafeModeSet:
      initialization_phase_ = InitializationPhase::kAuxPoweredOn;
      return SetAuxIoPower(/*target_enabled=*/true);
    case InitializationPhase::kAuxPoweredOn:
      initialization_phase_ = InitializationPhase::kInitialized;
      return true;
    case InitializationPhase::kInitialized:
      return false;
  }
}

bool TypeCDdiTigerLake::AdvanceDisableFsm() {
  switch (initialization_phase_) {
    case InitializationPhase::kUninitialized:
      return false;
    case InitializationPhase::kTypeCColdBlocked:
      if (UnblockTypeCColdPowerState()) {
        physical_layer_info_ = DefaultPhysicalLayerInfo();
        initialization_phase_ = InitializationPhase::kUninitialized;
        return true;
      }
      return false;
    case InitializationPhase::kSafeModeSet:
      if (SetPhySafeModeDisabled(/*target_disabled=*/false)) {
        initialization_phase_ = InitializationPhase::kTypeCColdBlocked;
        return true;
      }
      return false;
    case InitializationPhase::kAuxPoweredOn:
      if (SetAuxIoPower(/*target_enabled=*/false)) {
        initialization_phase_ = InitializationPhase::kSafeModeSet;
        return true;
      }
      return false;
    case InitializationPhase::kInitialized:
      initialization_phase_ = InitializationPhase::kAuxPoweredOn;
      return true;
  }
}

bool TypeCDdiTigerLake::Enable() {
  ZX_ASSERT(IsHealthy());

  // `IsHealthy()` returns true entails that the device is either in
  // `kInitialized` state where it needs to do nothing because of the function's
  // idempotency, or in `kUninitialized` state where it needs to start the
  // finite state machine.
  if (initialization_phase_ == InitializationPhase::kInitialized) {
    return true;
  }
  ZX_DEBUG_ASSERT(initialization_phase_ == InitializationPhase::kUninitialized);

  while (AdvanceEnableFsm()) {
  }
  if (initialization_phase_ == InitializationPhase::kInitialized) {
    zxlogf(TRACE, "DDI %d: Enabled. New physical layer info: %s", ddi_id(),
           physical_layer_info_.DebugString().c_str());
    return true;
  }
  while (AdvanceDisableFsm()) {
  }
  return false;
}

bool TypeCDdiTigerLake::Disable() {
  switch (initialization_phase_) {
    case InitializationPhase::kUninitialized:
      // Do nothing because of the function's idempotency.
      return true;
    case InitializationPhase::kInitialized:
      // Start the finite state machine of disable process.
      while (AdvanceDisableFsm()) {
      }
      if (initialization_phase_ == InitializationPhase::kUninitialized) {
        zxlogf(TRACE, "DDI %d: Disabled successfully.", ddi_id());
        return true;
      }
      [[fallthrough]];
    default:
      ZX_ASSERT(!IsHealthy());
      zxlogf(ERROR, "DDI %d: Failed to disable.", ddi_id());
      return false;
  }
}

bool TypeCDdiTigerLake::SetAuxIoPower(bool target_enabled) const {
  power_->SetAuxIoPowerState(ddi_id(), /* enable */ target_enabled);

  if (target_enabled) {
    if (!PollUntil([&] { return power_->GetAuxIoPowerState(ddi_id()); }, zx::usec(1), 1500)) {
      zxlogf(ERROR, "DDI %d: failed to enable AUX power for ddi", ddi_id());
      return false;
    }

    const bool is_thunderbolt =
        physical_layer_info_.connection_type == DdiPhysicalLayer::ConnectionType::kTypeCThunderbolt;
    if (!is_thunderbolt) {
      // For every Type-C port (static and DP Alternate but not thunderbolt),
      // the driver need to wait for the microcontroller health bit on
      // DKL_CMN_UC_DW27 register after enabling AUX power.
      //
      // TODO(fxbug.dev/99980): Currently Thunderbolt is not supported, so we
      // always check health bit of the IO subsystem microcontroller.
      //
      // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev 2.0, Page 417, "Type-C PHY
      //             Microcontroller health"
      if (!PollUntil(
              [&] {
                return tgl_registers::DekelCommonConfigMicroControllerDword27::GetForDdi(ddi_id())
                    .ReadFrom(mmio_space_)
                    .microcontroller_firmware_is_ready();
              },
              zx::usec(1), 10)) {
        zxlogf(ERROR, "DDI %d: microcontroller health bit is not set", ddi_id());
        return false;
      }
    }

    auto ddi_aux_ctl =
        tgl_registers::DdiAuxControl::GetForTigerLakeDdi(ddi_id()).ReadFrom(mmio_space_);
    ddi_aux_ctl.set_use_thunderbolt(is_thunderbolt);
    ddi_aux_ctl.WriteTo(mmio_space_);

    zxlogf(TRACE, "DDI %d: AUX IO power enabled", ddi_id());
  } else {
    zx::nanosleep(zx::deadline_after(zx::usec(10)));
    zxlogf(TRACE, "DDI %d: AUX IO power %sdisabled", ddi_id(),
           power_->GetAuxIoPowerState(ddi_id()) ? "not " : "");
  }

  return true;
}

bool TypeCDdiTigerLake::SetPhySafeModeDisabled(bool target_disabled) const {
  if (target_disabled && !tgl_registers::DynamicFlexIoDisplayPortPhyModeStatus::GetForDdi(ddi_id())
                              .ReadFrom(mmio_space_)
                              .phy_is_ready_for_ddi(ddi_id())) {
    zxlogf(ERROR, "DDI %d: lane not in DP mode", ddi_id());
    return false;
  }

  auto dp_csss =
      tgl_registers::DynamicFlexIoDisplayPortControllerSafeStateSettings::GetForDdi(ddi_id())
          .ReadFrom(mmio_space_);
  dp_csss.set_safe_mode_disabled_for_ddi(ddi_id(), /*disabled=*/target_disabled);
  dp_csss.WriteTo(mmio_space_);
  dp_csss.ReadFrom(mmio_space_);
  zxlogf(TRACE, "DDI %d: %s DP safe mode", ddi_id(), target_disabled ? "disabled" : "enabled");
  return true;
}

bool TypeCDdiTigerLake::BlockTypeCColdPowerState() {
  // TODO(fxbug.dev/111088): TCCOLD (Type C cold power state) blocking should
  // be decided at the display engine level. We may have already blocked TCCOLD
  // while bringing up another Type C DDI.
  zxlogf(TRACE, "Asking PCU firmware to block Type C cold power state");
  PowerController power_controller(mmio_space_);
  const zx::result<> power_status = power_controller.SetDisplayTypeCColdBlockingTigerLake(
      /*blocked=*/true, PowerController::RetryBehavior::kRetryUntilStateChanges);
  switch (power_status.status_value()) {
    case ZX_OK:
      zxlogf(TRACE, "PCU firmware blocked Type C cold power state");
      return true;
    default:
      zxlogf(ERROR, "Type C ports unusable. PCU firmware didn't block Type C cold power state: %s",
             power_status.status_string());
      return false;
  }
}

bool TypeCDdiTigerLake::UnblockTypeCColdPowerState() {
  // TODO(fxbug.dev/111088): TCCOLD (Type C cold power state) blocking should
  // be decided at the display engine level. We may have already blocked TCCOLD
  // while bringing up another Type C DDI.
  zxlogf(TRACE, "Asking PCU firmware to unblock Type C cold power state");
  PowerController power_controller(mmio_space_);
  const zx::result<> power_status = power_controller.SetDisplayTypeCColdBlockingTigerLake(
      /*blocked=*/false, PowerController::RetryBehavior::kNoRetry);
  switch (power_status.status_value()) {
    case ZX_OK:
      zxlogf(TRACE, "PCU firmware unblocked and entered Type C cold power state");
      return true;
    case ZX_ERR_IO_REFUSED:
      zxlogf(INFO,
             "PCU firmware did not enter Type C cold power state. "
             "Type C ports in use elsewhere.");
      return true;
    default:
      zxlogf(ERROR,
             "PCU firmware failed to unblock Type C cold power state. "
             "Type C ports unusable.");
      return false;
  }
}

}  // namespace i915_tgl
