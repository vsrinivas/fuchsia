// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pci-sdhci.h"

#include <fuchsia/hardware/pci/cpp/banjo.h>
#include <fuchsia/hardware/sdhci/cpp/banjo.h>
#include <inttypes.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/device-protocol/pci.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/status.h>

#include "src/devices/block/drivers/pci-sdhci/pci-sdhci-bind.h"

#define HOST_CONTROL1_OFFSET 0x28
#define SDHCI_EMMC_HW_RESET (1 << 12)

constexpr auto kTag = "pci-sdhci";

namespace sdhci {

PciSdhci::PciSdhci(zx_device_t* parent) : DeviceType(parent) {}

zx_status_t PciSdhci::SdhciGetInterrupt(zx::interrupt* interrupt_out) {
  // select irq mode
  pci_interrupt_mode_t mode = PCI_INTERRUPT_MODE_DISABLED;
  zx_status_t status = pci_.ConfigureInterruptMode(1, &mode);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: error setting irq mode: %s", kTag, zx_status_get_string(status));
    return status;
  }

  // get irq handle
  status = pci_.MapInterrupt(0, interrupt_out);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: error getting irq handle: %s", kTag, zx_status_get_string(status));
  }
  return status;
}

zx_status_t PciSdhci::SdhciGetMmio(zx::vmo* out, zx_off_t* out_offset) {
  if (!mmio_.has_value()) {
    zx_status_t status = pci_.MapMmio(0u, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: error mapping register window: %s", kTag, zx_status_get_string(status));
      return status;
    }
  }
  *out_offset = mmio_->get_offset();
  return mmio_->get_vmo()->duplicate(ZX_RIGHT_SAME_RIGHTS, out);
}

zx_status_t PciSdhci::SdhciGetBti(uint32_t index, zx::bti* out_bti) {
  if (!bti_.is_valid()) {
    zx_status_t st = pci_.GetBti(index, &bti_);
    if (st != ZX_OK) {
      return st;
    }
  }
  return bti_.duplicate(ZX_RIGHT_SAME_RIGHTS, out_bti);
}

uint32_t PciSdhci::SdhciGetBaseClock() { return 0; }

uint64_t PciSdhci::SdhciGetQuirks(uint64_t* out_dma_boundary_alignment) {
  *out_dma_boundary_alignment = 0;
  return SDHCI_QUIRK_STRIP_RESPONSE_CRC_PRESERVE_ORDER;
}

void PciSdhci::SdhciHwReset() {
  if (!mmio_.has_value()) {
    return;
  }
  uint32_t val = mmio_->Read32(HOST_CONTROL1_OFFSET);
  val |= SDHCI_EMMC_HW_RESET;
  mmio_->Write32(val, HOST_CONTROL1_OFFSET);
  // minimum is 1us but wait 9us for good measure
  zx_nanosleep(zx_deadline_after(ZX_USEC(9)));
  val &= ~SDHCI_EMMC_HW_RESET;
  mmio_->Write32(val, HOST_CONTROL1_OFFSET);
  // minimum is 200us but wait 300us for good measure
  zx_nanosleep(zx_deadline_after(ZX_USEC(300)));
}

void PciSdhci::DdkUnbind(ddk::UnbindTxn txn) { device_unbind_reply(zxdev()); }

zx_status_t PciSdhci::Bind(void* /* unused */, zx_device_t* parent) {
  auto dev = std::make_unique<PciSdhci>(parent);
  if (!dev) {
    zxlogf(ERROR, "%s: out of memory", kTag);
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = device_get_fragment_protocol(parent, "pci", ZX_PROTOCOL_PCI, &dev->pci_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: could not get PCI protocol: %s", kTag, zx_status_get_string(status));
    return status;
  }

  status = dev->pci_.SetBusMastering(true);
  if (status < 0) {
    zxlogf(ERROR, "%s: error in enable bus master: %s", kTag, zx_status_get_string(status));
    return status;
  }

  status = dev->DdkAdd(ddk::DeviceAddArgs("pci-sdhci").set_proto_id(ZX_PROTOCOL_SDHCI));
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: error adding device: %s", kTag, zx_status_get_string(status));
    return status;
  }

  // The object is owned by the DDK, now that it has been added. It will be deleted
  // when the device is released.
  __UNUSED auto ptr = dev.release();

  return ZX_OK;
}

static zx_driver_ops_t pci_sdhci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = PciSdhci::Bind,
};

void PciSdhci::DdkRelease() { delete this; }

}  // namespace sdhci

ZIRCON_DRIVER(pci_sdhci, sdhci::pci_sdhci_driver_ops, "zircon", "0.1");
