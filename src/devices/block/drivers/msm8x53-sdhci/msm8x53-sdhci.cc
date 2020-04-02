// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msm8x53-sdhci.h"

#include <lib/device-protocol/pdev.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>

#include "msm8x53-sdhci-reg.h"

namespace {

constexpr uint32_t kHcVendorSpecAddress = 0x10c;
constexpr uint32_t kHcVendorSpecResetValue = 0x0000'0a1c;

}  // namespace

namespace sdhci {

zx_status_t Msm8x53Sdhci::Create(void* ctx, zx_device_t* parent) {
  ddk::PDev pdev(parent);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "%s: ZX_PROTOCOL_PDEV not available\n", __FILE__);
    return ZX_ERR_NO_RESOURCES;
  }

  std::optional<ddk::MmioBuffer> core_mmio;
  zx_status_t status = pdev.MapMmio(0, &core_mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: MapMmio failed\n", __FILE__);
    return status;
  }

  std::optional<ddk::MmioBuffer> hc_mmio;
  if ((status = pdev.MapMmio(1, &hc_mmio)) != ZX_OK) {
    zxlogf(ERROR, "%s: MapMmio failed\n", __FILE__);
    return status;
  }

  zx::interrupt irq;
  if ((status = pdev.GetInterrupt(0, &irq)) != ZX_OK) {
    zxlogf(ERROR, "%s: Failed to map interrupt\n", __FILE__);
    return status;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<Msm8x53Sdhci> device(
      new (&ac) Msm8x53Sdhci(parent, *std::move(core_mmio), *std::move(hc_mmio), std::move(irq)));
  if (!ac.check()) {
    zxlogf(ERROR, "%s: Msm8x53Sdhci alloc failed\n", __FILE__);
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = device->Init()) != ZX_OK) {
    return status;
  }

  if ((status = device->DdkAdd("msm8x53-sdhci")) != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed\n", __FILE__);
    return status;
  }

  __UNUSED auto* dummy = device.release();

  return ZX_OK;
}

zx_status_t Msm8x53Sdhci::Init() {
  hc_mmio_.Write32(kHcVendorSpecResetValue, kHcVendorSpecAddress);

  HcVendorSpec3::Get().ReadFrom(&hc_mmio_).set_alt_fifo_en(0).WriteTo(&hc_mmio_);

  CoreHcMode::Get().FromValue(0).set_hc_mode_en(1).set_ff_clk_sw_rst_disable(1).WriteTo(
      &core_mmio_);

  return ZX_OK;
}

zx_status_t Msm8x53Sdhci::SdhciGetInterrupt(zx::interrupt* out_irq) {
  out_irq->reset(irq_.release());
  return ZX_OK;
}

zx_status_t Msm8x53Sdhci::SdhciGetMmio(zx::vmo* out_mmio, zx_off_t* out_offset) {
  hc_mmio_.get_vmo()->duplicate(ZX_RIGHT_SAME_RIGHTS, out_mmio);
  *out_offset = hc_mmio_.get_offset();
  return ZX_OK;
}

zx_status_t Msm8x53Sdhci::SdhciGetBti(uint32_t index, zx::bti* out_bti) {
  ddk::PDev pdev(parent());
  if (!pdev.is_valid()) {
    return ZX_ERR_NO_RESOURCES;
  }

  return pdev.GetBti(index, out_bti);
}

uint32_t Msm8x53Sdhci::SdhciGetBaseClock() { return 0; }

uint64_t Msm8x53Sdhci::SdhciGetQuirks(uint64_t* out_dma_boundary_alignment) {
  *out_dma_boundary_alignment = 0;
  return SDHCI_QUIRK_STRIP_RESPONSE_CRC_PRESERVE_ORDER | SDHCI_QUIRK_NO_DMA |
         SDHCI_QUIRK_NON_STANDARD_TUNING;
}

void Msm8x53Sdhci::SdhciHwReset() {}

}  // namespace sdhci

static constexpr zx_driver_ops_t msm8x53_sdhci_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = sdhci::Msm8x53Sdhci::Create;
  return ops;
}();

ZIRCON_DRIVER_BEGIN(msm8x53_sdhci, msm8x53_sdhci_driver_ops, "zircon", "0.1", 3)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_QUALCOMM),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_QUALCOMM_SDC1), ZIRCON_DRIVER_END(msm8x53_sdhci)
