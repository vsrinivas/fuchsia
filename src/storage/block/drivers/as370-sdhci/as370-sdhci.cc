// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "as370-sdhci.h"

#include <lib/device-protocol/pdev.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>

namespace sdhci {

zx_status_t As370Sdhci::Create(void* ctx, zx_device_t* parent) {
  ddk::PDev pdev(parent);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_PDEV not available\n", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  } else {
    pdev.ShowInfo();
  }

  std::optional<ddk::MmioBuffer> core_mmio;

  zx_status_t status = pdev.MapMmio(0, &core_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: MapMmio failed\n", __FILE__);
    return status;
  }
  zx::interrupt irq;
  if ((status = pdev.GetInterrupt(0, &irq)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to map interrupt\n", __FILE__);
    return status;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<As370Sdhci> device(new (&ac)
                                         As370Sdhci(parent, *std::move(core_mmio), std::move(irq)));
  if (!ac.check()) {
    zxlogf(ERROR, "%s: As370Sdhci alloc failed\n", __FILE__);
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = device->Init()) != ZX_OK) {
    return status;
  }

  if ((status = device->DdkAdd("as370-sdhci")) != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed\n", __FILE__);
    return status;
  }

  __UNUSED auto* dummy = device.release();

  return ZX_OK;
}

zx_status_t As370Sdhci::Init() { return ZX_OK; }

zx_status_t As370Sdhci::SdhciGetInterrupt(zx::interrupt* out_irq) {
  out_irq->reset(irq_.release());
  return ZX_OK;
}

zx_status_t As370Sdhci::SdhciGetMmio(zx::vmo* out_mmio, zx_off_t* out_offset) {
  core_mmio_.get_vmo()->duplicate(ZX_RIGHT_SAME_RIGHTS, out_mmio);
  *out_offset = core_mmio_.get_offset();
  return ZX_OK;
}

zx_status_t As370Sdhci::SdhciGetBti(uint32_t index, zx::bti* out_bti) {
  ddk::PDev pdev(parent());
  if (!pdev.is_valid()) {
    return ZX_ERR_NO_RESOURCES;
  }

  return pdev.GetBti(index, out_bti);
}

uint32_t As370Sdhci::SdhciGetBaseClock() { return 0; }

uint64_t As370Sdhci::SdhciGetQuirks() { return SDHCI_QUIRK_STRIP_RESPONSE_CRC_PRESERVE_ORDER; }

void As370Sdhci::SdhciHwReset() {}

}  // namespace sdhci

static constexpr zx_driver_ops_t as370_sdhci_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = sdhci::As370Sdhci::Create;
  return ops;
}();

ZIRCON_DRIVER_BEGIN(as370_sdhci, as370_sdhci_driver_ops, "zircon", "0.1", 3)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_SYNAPTICS),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AS370_SDHCI0), ZIRCON_DRIVER_END(as370_sdhci)
