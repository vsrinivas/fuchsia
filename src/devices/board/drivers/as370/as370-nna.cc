// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>

#include <soc/as370/as370-nna.h>

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"
#include "as370.h"
#include "src/devices/board/drivers/as370/as370_nna_bind.h"

namespace board_as370 {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> nna_mmios{
    []() {
        fpbus::Mmio ret;
        ret.base() = as370::kNnaBase;
        ret.length() = as370::kNnaSize;
        return ret;
    }(),
};

static const std::vector<fpbus::Bti> nna_btis{
    []() {
        fpbus::Bti ret;
        ret.iommu_index() = 0;
        ret.bti_id() = BTI_NNA;
        return ret;
    }(),
};

static const std::vector<fpbus::Irq> nna_irqs{
    []() {
        fpbus::Irq ret;
        ret.irq() = as370::kNnaIrq;
        ret.mode() = ZX_INTERRUPT_MODE_LEVEL_HIGH;
        return ret;
    }(),
};

static const fpbus::Node nna_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "as370-nna";
  dev.vid() = PDEV_VID_SYNAPTICS;
  dev.pid() = PDEV_PID_SYNAPTICS_AS370;
  dev.did() = PDEV_DID_AS370_NNA;
  dev.mmio() = nna_mmios;
  dev.bti() = nna_btis;
  dev.irq() = nna_irqs;
  return dev;
}();

zx_status_t As370::NnaInit() {
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('NNA_');
  auto result = pbus_.buffer(arena)->AddComposite(fidl::ToWire(fidl_arena, nna_dev), platform_bus_composite::MakeFidlFragment(fidl_arena, as370_nna_fragments, std::size(as370_nna_fragments)), "pdev");
  if (!result.ok()) {
    zxlogf(ERROR, "%s: DeviceAdd Nna request failed: %s", __func__, result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: DeviceAdd Nna failed: %s", __func__, zx_status_get_string(result->error_value()));
    return result->error_value();
  }
  return ZX_OK;
}

}  // namespace board_as370
