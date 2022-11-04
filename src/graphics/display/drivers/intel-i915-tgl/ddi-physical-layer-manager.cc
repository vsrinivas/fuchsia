// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/ddi-physical-layer-manager.h"

#include <zircon/assert.h>

#include "src/graphics/display/drivers/intel-i915-tgl/ddi-physical-layer.h"
#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"
#include "src/graphics/display/drivers/intel-i915-tgl/intel-i915-tgl.h"

namespace i915_tgl {

DdiReference::DdiReference() = default;

DdiReference::DdiReference(DdiPhysicalLayer* phy) : phy_(phy) {
  ZX_DEBUG_ASSERT(phy_);
  phy_->AddRef();
}

DdiReference::~DdiReference() {
  if (phy_) {
    phy_->Release();
  }
}

DdiReference::DdiReference(DdiReference&& rhs) noexcept {
  phy_ = rhs.phy_;
  rhs.phy_ = nullptr;
}

DdiReference& DdiReference::operator=(DdiReference&& rhs) noexcept {
  if (phy_) {
    phy_->Release();
  }

  phy_ = rhs.phy_;
  rhs.phy_ = nullptr;
  return *this;
}

DdiPhysicalLayer::PhysicalLayerInfo DdiReference::GetPhysicalLayerInfo() const {
  ZX_DEBUG_ASSERT(phy_);
  return phy_->GetPhysicalLayerInfo();
}

DdiReference DdiManager::GetDdiReference(DdiId ddi_id) {
  auto ddi_state_iter = ddi_map_.find(ddi_id);
  ZX_DEBUG_ASSERT_MSG(ddi_state_iter != ddi_map_.end(),
                      "DdiManager: DDI %d is not available. Cannot request reference for DDI.",
                      ddi_id);
  DdiPhysicalLayer* phy = ddi_state_iter->second.get();
  // If there's no existing reference to the PHY, we'll need to enable the PHY
  // first.
  ZX_DEBUG_ASSERT(phy);
  if (phy->Enable()) {
    return DdiReference(phy);
  }
  return DdiReference();
}

DdiManagerSkylake::DdiManagerSkylake() {
  for (const DdiId ddi_id : DdiIds<tgl_registers::Platform::kSkylake>()) {
    ddi_map()[ddi_id] = std::make_unique<DdiSkylake>(ddi_id);
  }
}

DdiManagerTigerLake::DdiManagerTigerLake(Controller* controller)
    : DdiManagerTigerLake(controller->power(), controller->mmio_space(),
                          controller->igd_opregion()) {}

DdiManagerTigerLake::DdiManagerTigerLake(Power* power, fdf::MmioBuffer* mmio_space,
                                         const IgdOpRegion& igd_opregion) {
  for (const DdiId ddi_id : DdiIds<tgl_registers::Platform::kTigerLake>()) {
    if (!igd_opregion.HasDdi(ddi_id)) {
      continue;
    }
    if (ddi_id >= DdiId::DDI_A && ddi_id <= DdiId::DDI_C) {
      // COMBO DDI
      ddi_map()[ddi_id] = std::make_unique<ComboDdiTigerLake>(ddi_id);
    } else if (ddi_id >= DdiId::DDI_TC_1 && ddi_id <= DdiId::DDI_TC_6) {
      // Type-C DDI
      const bool is_static_port = !igd_opregion.IsTypeC(ddi_id);
      ddi_map()[ddi_id] =
          std::make_unique<TypeCDdiTigerLake>(ddi_id, power, mmio_space, is_static_port);
    }
  }
}

}  // namespace i915_tgl
