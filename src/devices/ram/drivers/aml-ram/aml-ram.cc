// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-ram.h"

#include <lib/device-protocol/pdev.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddktl/fidl.h>
#include <fbl/alloc_checker.h>

namespace amlogic_ram {

zx_status_t AmlRam::Create(void* context, zx_device_t* parent) {
  zx_status_t status;
  ddk::PDev pdev(parent);
  std::optional<ddk::MmioBuffer> mmio;
  if ((status = pdev.MapMmio(0, &mmio)) != ZX_OK) {
    zxlogf(ERROR, "aml-ram: Failed to map mmio, st = %d\n", status);
    return status;
  }

  fbl::AllocChecker ac;
  auto device = fbl::make_unique_checked<AmlRam>(&ac, parent, *std::move(mmio));
  if (!ac.check()) {
    zxlogf(ERROR, "aml-ram: Failed to allocate device memory\n");
    return ZX_ERR_NO_MEMORY;
  }

  status = device->DdkAdd("ram", DEVICE_ADD_NON_BINDABLE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-ram: Failed to add ram device, st = %d\n", status);
    return status;
  }

  __UNUSED auto* dummy = device.release();
  return ZX_OK;
}

AmlRam::AmlRam(zx_device_t* parent, ddk::MmioBuffer mmio)
    : DeviceType(parent), mmio_(std::move(mmio)) {}

void AmlRam::DdkRelease() {
  zxlogf(INFO, "aml-ram release");
  delete this;
}

zx_status_t AmlRam::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  // TODO(cpu): Implement dispatch of the fuchsia.hardware.ram.metrics
  // FIDL interface.
  return transaction.Status();
}

}  // namespace amlogic_ram

static constexpr zx_driver_ops_t aml_ram_driver_ops = []() {
  zx_driver_ops_t result = {};
  result.version = DRIVER_OPS_VERSION;
  result.bind = amlogic_ram::AmlRam::Create;
  return result;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(aml_ram, aml_ram_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_RAM_CTL),
    // This driver can likely support S905D3 and T931 in the future.
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D2),
ZIRCON_DRIVER_END(aml_ram)
    // clang-format on
