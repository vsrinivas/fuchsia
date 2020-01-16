// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-cpu.h"

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddktl/fidl.h>

namespace amlogic_cpu {

zx_status_t AmlCpu::Create(void* context, zx_device_t* device) {
  zx_status_t status;

  auto cpu_device = std::make_unique<AmlCpu>(device);

  status = cpu_device->DdkAdd("cpu", DEVICE_ADD_NON_BINDABLE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-cpu: Failed to add cpu device, st = %d\n", status);
    return status;
  }

  // Intentionally leak this device because it's owned by the driver framework.
  __UNUSED auto unused = cpu_device.release();

  return ZX_OK;
}

zx_status_t AmlCpu::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  fuchsia_cpuctrl::Device::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void AmlCpu::DdkRelease() { delete this; }

void AmlCpu::GetPerformanceStateInfo(uint32_t state,
                                     GetPerformanceStateInfoCompleter::Sync completer) {
  // Placeholder.
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void AmlCpu::GetNumLogicalCores(GetNumLogicalCoresCompleter::Sync completer) {
  // Placeholder.
  completer.Reply(0);
}

void AmlCpu::GetLogicalCoreId(uint64_t index, GetLogicalCoreIdCompleter::Sync completer) {
  // Placeholder.
  completer.Reply(0);
}

}  // namespace amlogic_cpu

static constexpr zx_driver_ops_t aml_cpu_driver_ops = []() {
  zx_driver_ops_t result = {};
  result.version = DRIVER_OPS_VERSION;
  result.bind = amlogic_cpu::AmlCpu::Create;
  return result;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(aml_cpu, aml_cpu_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D2),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_CPU),
ZIRCON_DRIVER_END(aml_cpu)
