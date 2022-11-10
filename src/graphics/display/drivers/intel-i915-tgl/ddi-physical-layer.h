// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_DDI_PHYSICAL_LAYER_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_DDI_PHYSICAL_LAYER_H_

#include <zircon/assert.h>

#include <fbl/string.h>

#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"
#include "src/graphics/display/drivers/intel-i915-tgl/power.h"

namespace i915_tgl {

class DdiReference;

// On Intel display devices, DDIs (Digital Display Interfaces) contain port
// logic to interface to the DDI physical layer (PHY), which are the physical
// ports in the IO subsystem provided by the hardware.
//
// This class provides interfaces of the physical layers to display drivers, so
// that the drivers can:
// - `Enable()` / `Disable()` the physical layer of a certain port for display
//   use;
// - Query display device availability (`GetPhysicalLayerInfo()`) on the
//   physical port.
//
// A typical DDI Physical Layer can communicate with the port IO subsystem to
// maintain power state of the physical port, configure physical lanes for
// display usage, and query physical device state to report back to the driver.
//
// On older generations of Intel Display Engine (e.g. Kaby Lake and Skylake),
// the DDI Physical layers are usually automatically configured by the firmware
// and the driver doesn't need to do much to maintain the physical layer state.
// However on newer generations (e.g. Ice Lake, Tiger Lake), drivers must
// initialize the physical layer before using it for display purpose.
//
// `DdiPhysicalLayer`s are intrusively reference counted. Display Devices can
// hold references to enabled PHYs, and release the reference once the display
// is removed, which finally disable the PHY for power saving when the PHY is
// not referenced by any display.
//
// The Ref-counting Is *Not* Thread-safe. `DdiPhysicalLayer`s and references to
// `DdiPhysicalLayer`s must be accessed only by a single thread.
// TODO(fxbug.dev/112849): Currently the intel-i915-tgl driver doesn't fulfill
// this requirement. The threading model of the driver needs to be fixed.
//
// References:
//
// Ice Lake:
// - IHD-OS-ICLLP-Vol 12-1.22-Rev 2.0 Pages 333-335 "Digital Display Interface"
// - IHD-OS-ICLLP-Vol 12-1.22-Rev 2.0 Pages 346-360 "Gen11+ TypeC Programming"
//
// Tiger Lake:
// - IHD-OS-TGL-Vol 12-1.22-Rev 2.0 Pages 390-398 "Digital Display Interface"
// - IHD-OS-TGL-Vol 12-1.22-Rev 2.0 Pages 399-409 "TypeC Programming"
class DdiPhysicalLayer {
 public:
  enum class DdiType {
    // COMBO DDI (DDI A - DDI C) on Tiger Lake
    // DDI (DDI A - DDI E) on Skylake / Kaby Lake
    kCombo,

    // Type-C (Dekel) DDI (DDI TC 1 - DDI TC 6) on Tiger Lake
    kTypeC,
  };

  enum class ConnectionType {
    // The DDI has no physical port attached or no display is plugged to the
    // port.
    kNone,

    // A built-in HDMI or DisplayPort connector is attached to the DDI to
    // support a fixed configuration.
    kBuiltIn,

    // A USB Type-C connector is attached to the DDI and a Type-C device is
    // connected using DisplayPort Alternate mode.
    kTypeCDisplayPortAltMode,

    // A USB Type-C connector is attached to the DDI and a Type-C device is
    // connected using DisplayPort over Thunderbolt mode.
    kTypeCThunderbolt,
  };

  struct PhysicalLayerInfo {
    // The type of DDI.
    //
    // See definitions of `DdiType` enum class above for valid values.
    DdiType ddi_type;

    // The type of port / device attached to the DDI.
    //
    // If the value is not `kNone`, it means a display device *may* be
    // connected. The driver then should use GMBUS (for DVI / HDMI) or DPCD
    // registers (for DisplayPort) to determine display availability.
    ConnectionType connection_type;

    // This is the physical layer's constraint on the connection's lane count.
    //
    // There may be other sides (for example, the DisplayPort capability by
    // sink device) which could introduce additional constraints.
    //
    // The driver must use the *minimum* lane count value so that it fulfills
    // all the constraints.
    uint8_t max_allowed_dp_lane_count = 0;

    fbl::String DebugString() const;
  };

  explicit DdiPhysicalLayer(DdiId ddi_id) : ddi_id_(ddi_id) {}
  virtual ~DdiPhysicalLayer() = default;

  // Copying and moving are not allowed.
  DdiPhysicalLayer(const DdiPhysicalLayer&) = delete;
  DdiPhysicalLayer& operator=(const DdiPhysicalLayer&) = delete;
  DdiPhysicalLayer(DdiPhysicalLayer&&) = delete;
  DdiPhysicalLayer& operator=(DdiPhysicalLayer&&) = delete;

  DdiId ddi_id() const { return ddi_id_; }

  // Indicates whether the DDI PHY is already enabled.
  virtual bool IsEnabled() const = 0;

  // Indicates whether the DDI PHY is in a healthy state to be enabled.
  // Drivers must not `Enable()` a DDI or use it for display if `IsHealthy()`
  // returns false.
  virtual bool IsHealthy() const = 0;

  // Enables the physical layer of the DDI.
  //
  // Returns true if the DDI PHY is enabled successfully, otherwise returns
  // false.
  //
  // This method is idempotent; if a DDI PHY is already enabled when this
  // function is called, the method will not change the hardware state.
  virtual bool Enable() = 0;

  // Disables the physical layer of the DDI.
  //
  // Returns true if the DDI PHY is disabled successfully, otherwise returns
  // false.
  //
  // This method is idempotent; if a DDI PHY is already disabled when this
  // function is called, the method will not change the hardware state.
  virtual bool Disable() = 0;

  virtual PhysicalLayerInfo GetPhysicalLayerInfo() const = 0;

 private:
  friend class DdiReference;

  // Adds a reference to an enabled PHY.
  void AddRef();

  // Releases a reference to this object. This will disable the PHY once the
  // last reference is released.
  void Release();

  DdiId ddi_id_;

  // The ref-counting is *not* thread-safe.
  int ref_count_ = 0;
};

// Instantiation of DDI Physical Layer (DDI A-E) on Skylake / Kaby Lake.
class DdiSkylake : public DdiPhysicalLayer {
 public:
  explicit DdiSkylake(DdiId ddi_id) : DdiPhysicalLayer(ddi_id) {}
  ~DdiSkylake() override = default;

  // DdiPhysicalLayer overrides:
  bool IsEnabled() const override { return enabled_; }
  bool IsHealthy() const override { return true; }
  bool Enable() override;
  bool Disable() override;
  PhysicalLayerInfo GetPhysicalLayerInfo() const override;

 private:
  bool enabled_ = false;
};

// Tiger Lake's Combo DDIs (DDI A-C).
//
// Combo DDIs support both high-voltage display standards (DisplayPort, HDMI)
// suitable for long backplanes (cables connected to external monitors) and
// as low-voltage standards (Embedded DisplayPort, MIPI D-PHY) used for shorter
// backplanes (PCB traces and short internal cables).
//
// Each combo DDI is connected to a specific port type at device manufacturing
// time. The connectivity information is recorded in the VBT (Video BIOS
// Table). The display driver (us) is responsible for configuring the DDI to
// reflect this information.
class ComboDdiTigerLake : public DdiPhysicalLayer {
 public:
  explicit ComboDdiTigerLake(DdiId ddi_id, fdf::MmioBuffer* mmio_space);
  ~ComboDdiTigerLake() override = default;

  // DdiPhysicalLayer overrides:
  bool IsEnabled() const override { return enabled_; }
  bool IsHealthy() const override { return true; }
  bool Enable() override;
  bool Disable() override;
  PhysicalLayerInfo GetPhysicalLayerInfo() const override;

  // Combo PHYs must be initialized before being enabled.
  // TODO(fxbug.dev/114769): Create an initialization API in the base class.
  bool Initialize();

  // Combo PHYs must be un-initialized before entering the DC9 sleep state.
  // TODO(fxbug.dev/114769): Create an initialization API in the base class.
  bool Deinitialize();

 private:
  bool enabled_ = false;
  fdf::MmioBuffer* const mmio_space_ = nullptr;
};

// Instantiation of Type-C DDI Physical Layer (DDI TC 1-6) on Tiger Lake.
class TypeCDdiTigerLake : public DdiPhysicalLayer {
 public:
  TypeCDdiTigerLake(DdiId ddi_id, Power* power, fdf::MmioBuffer* mmio_space, bool is_static_port);
  ~TypeCDdiTigerLake() override;

  // The DDI PHY initialization process contains multiple steps and can be
  // described as a linear finite state machine (FSM).
  // - For a successful initialization process, it starts from "Uninitialized"
  //   and ends at "Initialized".
  //
  //      Uninitialized
  //            |
  //            v
  //      Intermediate State 1
  //            |
  //            v
  //      Intermediate State 2
  //            |
  //           ...
  //            |
  //            v
  //      Initialized
  //
  // - For a failed initialization process, it starts from "Uninitialized",
  //   transitioning to intermediate steps until initialization fails, and then
  //   traverses back to "Uninitialized" (if deinitialization succeeds,
  //   otherwise it will stop at the first failed deinitialization step) along
  //   the reversed direction.
  //
  //      Uninitialized
  //        |    ^
  //        v    |
  //      Intermediate State 1
  //        |    ^
  //        v    |
  //      Intermediate State 2 (init failed)
  //
  //        or
  //
  //      Uninitialized
  //        |
  //        v
  //      Intermediate State 1 (deinit failed)
  //        |    ^
  //        v    |
  //      Intermediate State 2 (failed)
  //
  // - For deinitialization process, it starts from "Initialized" and ends at
  //   "Uninitialized" (if it succeeds) or the first intermediate state it
  //   failed at.
  //
  //      Uninitialized
  //            ^
  //            |
  //      Intermediate State 1
  //            ^
  //            |
  //           ...
  //            |
  //      Intermediate State N-1
  //            ^
  //            |
  //      Intermediate State N
  //            ^
  //            |
  //      Initialized
  //
  // All the valid states, including "Uninitialized", "Initialized", and all
  // intermediate states will be defined in this class.
  enum class InitializationPhase : int;

  // DdiPhysicalLayer overrides:
  bool IsEnabled() const override;
  bool IsHealthy() const override;
  bool Enable() override;
  bool Disable() override;
  PhysicalLayerInfo GetPhysicalLayerInfo() const override { return physical_layer_info_; }

  // Helper method to read `PhysicalLayerInfo` directly from hardware registers.
  //
  // Caller must guarantee that this is only called when the Type-C
  // microcontroller is ready.
  PhysicalLayerInfo ReadPhysicalLayerInfo() const;

  // Advance the FSM in the "enable" direction (towards "Initialized") for one
  // step.
  //
  // The return value indicates whether the "enable" FSM should continue
  // running.
  // Returns false if and only if
  // - The FSM is already at the terminal state (Initialized), or
  // - The step taken fails.
  //
  // This public interface should be only used by tests.
  bool AdvanceEnableFsm();

  // Advance the FSM in the "disable" direction (towards "Uninitialized") for
  // one step.
  //
  // The return value indicates whether the "disable" FSM should continue
  // running.
  // Returns false if and only if
  // - The FSM is already at the terminal state (Uninitialized), or
  // - The step taken fails.
  //
  // This public interface should be only used by tests.
  bool AdvanceDisableFsm();

  const InitializationPhase& GetInitializationPhaseForTesting() const {
    return initialization_phase_;
  }
  void SetInitializationPhaseForTesting(InitializationPhase initialization_phase) {
    initialization_phase_ = initialization_phase;
  }

 private:
  // Default physical layer state when there is no display plugged in.
  PhysicalLayerInfo DefaultPhysicalLayerInfo() const {
    return PhysicalLayerInfo{
        .ddi_type = DdiType::kTypeC,
        .connection_type = is_static_port_ ? ConnectionType::kBuiltIn : ConnectionType::kNone,
        .max_allowed_dp_lane_count = static_cast<uint8_t>(is_static_port_ ? 4 : 0),
    };
  }

  bool SetAuxIoPower(bool target_enabled) const;
  bool SetPhySafeModeDisabled(bool target_disabled) const;

  bool BlockTypeCColdPowerState();
  bool UnblockTypeCColdPowerState();

  Power* power_ = nullptr;
  fdf::MmioBuffer* mmio_space_ = nullptr;

  // On device initialization, this stands for the last initialization step that
  // was attempted. This step might not have completed successfully.
  //
  // On device deinitialization, this stands for the last initialization step
  // that has not yet been reverted successfully (i.e., the revert step might
  // not have happened yet, or the revert step has just failed).
  InitializationPhase initialization_phase_;

  bool is_static_port_ = false;
  PhysicalLayerInfo physical_layer_info_;
};

}  // namespace i915_tgl

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_DDI_PHYSICAL_LAYER_H_
