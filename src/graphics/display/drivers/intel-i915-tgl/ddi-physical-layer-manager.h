// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_DDI_PHYSICAL_LAYER_MANAGER_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_DDI_PHYSICAL_LAYER_MANAGER_H_

#include <lib/mmio/mmio-buffer.h>

#include <unordered_map>

#include "src/graphics/display/drivers/intel-i915-tgl/ddi-physical-layer.h"
#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"
#include "src/graphics/display/drivers/intel-i915-tgl/igd.h"
#include "src/graphics/display/drivers/intel-i915-tgl/power.h"

namespace i915_tgl {

class DdiManager;

// An RAII helper class for lifetime management of intrusively refcounted DDI
// Physical Interfaces.
//
// A typical usage scenario of `DdiReference` is a `DisplayDevice` owning
// `DdiReference` to keep the DDI physical layer enabled when the display is on,
// and destroying the `DdiReference` to release ownership of the PHY when
// tearing down the display, which will finally turn off the PHY once all the
// display devices are removed.
//
// Note that since `DdiPhysicalLayer` is not thread-safe, all the
// `DdiPhysicalLayer`s and `DdiReference`s should be created, accessed and
// destroyed by only one single thread.
class DdiReference {
 public:
  // Null reference.
  DdiReference();

  // Reference to a `phy` owned and managed by `manager`.
  explicit DdiReference(DdiPhysicalLayer* phy);
  ~DdiReference();

  // Not copyable.
  DdiReference(const DdiReference&) = delete;
  DdiReference& operator=(const DdiReference&) = delete;

  // Moveable.
  DdiReference(DdiReference&& rhs) noexcept;
  DdiReference& operator=(DdiReference&& rhs) noexcept;

  bool IsNull() const { return phy_ == nullptr; }
  explicit operator bool() const { return !IsNull(); }

  // This forwards return value of `DdiPhysicalLayer::GetPhysicalLayerInfo()`.
  // Callers should only call this on a non-null `DdiReference` object.
  DdiPhysicalLayer::PhysicalLayerInfo GetPhysicalLayerInfo() const;

 private:
  DdiPhysicalLayer* phy_ = nullptr;
};

// The DDI Manager stores all DDI PHY instances and creates references to
// ref-counted DDI physical layer instances for DisplayDevice.
//
// This class cannot be instantiated by itself. Platforms (e.g. Skylake / Tiger
// Lake) must inherit from this class to create platform-specific `DdiManager`
// instances.
class DdiManager {
 public:
  // Try to create a `DdiReference` (a reference to DDI physical layer
  // interface) for `ddi`.
  //
  // Callers must guarantee that `ddi` is invalid on the Display Engine and
  // corresponds to a valid physical port on the board.
  //
  // Returns a null `DdiReference` if the physical layer of `ddi` cannot be
  // enabled. Otherwise, it returns a `DdiReference` to the enabled DDI physical
  // interface.
  DdiReference GetDdiReference(tgl_registers::Ddi ddi);

 protected:
  using DdiIdToPhyMap = std::unordered_map<tgl_registers::Ddi, std::unique_ptr<DdiPhysicalLayer>>;

  // Made protected so that this class cannot be instantiated by itself.
  DdiManager() = default;

  const DdiIdToPhyMap& ddi_map() const { return ddi_map_; }
  DdiIdToPhyMap& ddi_map() { return ddi_map_; }

 private:
  DdiIdToPhyMap ddi_map_;
};

// Instantiation of DDI Manager on Skylake / Kaby Lake.
class DdiManagerSkylake : public DdiManager {
 public:
  DdiManagerSkylake();
};

// Instantiation of DDI Manager on Tiger Lake.
class DdiManagerTigerLake : public DdiManager {
 public:
  explicit DdiManagerTigerLake(Controller* controller);

  // Used for testing only.
  // Tests can use this class to inject all the classes used to create DDI PHY
  // instances.
  DdiManagerTigerLake(Power* power, fdf::MmioBuffer* mmio_space, const IgdOpRegion& igd_opregion);
};

}  // namespace i915_tgl

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_DDI_PHYSICAL_LAYER_MANAGER_H_
