// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "as370-nna.h"

#include <fuchsia/hardware/registers/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <stdlib.h>
#include <unistd.h>
#include <zircon/types.h>

#include <memory>

#include <bind/fuchsia/platform/cpp/bind.h>
#include <bind/fuchsia/verisilicon/platform/cpp/bind.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

#include "src/devices/lib/as370/include/soc/as370/as370-nna.h"
#include "src/devices/ml/drivers/as370-nna/as370_nna_bind.h"

namespace as370_nna {

// This is to be compatible with magma::ZirconPlatformDevice.
zx_status_t As370NnaDevice::DdkGetProtocol(uint32_t proto_id, void* out_protocol) {
  auto* proto = static_cast<ddk::AnyProtocol*>(out_protocol);
  proto->ctx = parent_pdev_.ctx;
  switch (proto_id) {
    case ZX_PROTOCOL_PDEV:
      proto->ops = parent_pdev_.ops;
      return ZX_OK;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t As370NnaDevice::Init() {
  // Reset NNA HW
  auto clear_result =
      global_registers_->WriteRegister32(as370::kNnaResetOffset, as370::kNnaResetMask, 0);
  if ((clear_result.status() != ZX_OK) || clear_result->is_error()) {
    zxlogf(ERROR, "Clear Reset Write failed");
    return ZX_ERR_INTERNAL;
  }

  // Enable NNA HW
  auto set_result =
      global_registers_->WriteRegister32(as370::kNnaResetOffset, as370::kNnaResetMask, 1);
  if ((set_result.status() != ZX_OK) || set_result->is_error()) {
    zxlogf(ERROR, "Set Reset Write failed");
    return ZX_ERR_INTERNAL;
  }

  // TODO(fxbug.dev/109441): Use fuchsia.hardware.power/Power
  // Enable power to the NNA block
  auto power_result =
      global_registers_->WriteRegister32(as370::kNnaPowerOffset, as370::kNnaPowerMask, 0);
  if ((power_result.status() != ZX_OK) || power_result->is_error()) {
    zxlogf(ERROR, "Power Write failed");
    return ZX_ERR_INTERNAL;
  }

  // TODO(fxbug.dev/109443): Use fuchsia.hardware.clock/Clock
  // Configure NNA AXI Clock
  // ClkSel       = 0x101   divide by 12
  // Clk3DSwitch  = 0b0     No diveder
  // ClkSwitch    = 0b0     No divider
  // CLkPIISwitch = 0b1     AVPLL
  // ClkPIISel    = 0b100   SYSPLL DIV3
  // ClkEn        = 0b1     Enabled
  auto set_clocksys =
      global_registers_->WriteRegister32(as370::kNnaClockSysOffset, as370::kNnaClockSysMask, 0x299);
  if ((set_clocksys.status() != ZX_OK) || set_clocksys->is_error()) {
    zxlogf(ERROR, "Set clock1 Write failed");
    return ZX_ERR_INTERNAL;
  }

  // TODO(fxbug.dev/109443): Use fuchsia.hardware.clock/Clock
  // Configure NNA Core Clock
  // ClkSel       = 0x101   divide by 12
  // Clk3DSwitch  = 0b0     No diveder
  // ClkSwitch    = 0b0     No divider
  // CLkPIISwitch = 0b1     AVPLL
  // ClkPIISel    = 0b100   SYSPLL DIV3
  // ClkEn        = 0b1     Enabled
  auto set_clockcore = global_registers_->WriteRegister32(as370::kNnaClockCoreOffset,
                                                          as370::kNnaClockCoreMask, 0x299);
  if ((set_clockcore.status() != ZX_OK) || set_clockcore->is_error()) {
    zxlogf(ERROR, "Set clock2 Write failed");
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

// static
zx_status_t As370NnaDevice::Create(void* ctx, zx_device_t* parent) {
  zx_status_t status;

  ddk::PDev pdev = ddk::PDev::FromFragment(parent);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "Could not get platform device protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  pdev_device_info_t info;
  status = pdev.GetDeviceInfo(&info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "GetDeviceInfo failed %s", zx_status_get_string(status));
    return status;
  }

  if (info.pid != PDEV_PID_SYNAPTICS_AS370) {
    zxlogf(ERROR, "Unhandled PID 0x%x", info.pid);
    return ZX_ERR_INVALID_ARGS;
  }

  ddk::RegistersProtocolClient reset(parent, "register-reset");
  if (!reset.is_valid()) {
    zxlogf(ERROR, "Could not get global_registers fragment");
    return ZX_ERR_NO_RESOURCES;
  }

  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_registers::Device>();
  if (endpoints.is_error()) {
    zxlogf(ERROR, "Could not create channel %s", endpoints.status_string());
    return endpoints.error_value();
  }
  reset.Connect(endpoints->server.TakeChannel());
  auto reset_register = fidl::WireSyncClient(std::move(endpoints->client));

  fbl::AllocChecker ac;

  auto device = std::unique_ptr<As370NnaDevice>(
      new (&ac) As370NnaDevice(parent, std::move(reset_register), std::move(pdev)));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = device->Init()) != ZX_OK) {
    zxlogf(ERROR, "Could not init device %s", zx_status_get_string(status));
    return status;
  }

  zx_device_prop_t props[] = {
      {BIND_PROTOCOL, 0, bind_fuchsia_platform::BIND_PROTOCOL_DEVICE},
      {BIND_PLATFORM_DEV_VID, 0,
       bind_fuchsia_verisilicon_platform::BIND_PLATFORM_DEV_VID_VERISILICON},
      {BIND_PLATFORM_DEV_PID, 0, bind_fuchsia_platform::BIND_PLATFORM_DEV_PID_GENERIC},
      {BIND_PLATFORM_DEV_DID, 0,
       bind_fuchsia_verisilicon_platform::BIND_PLATFORM_DEV_DID_MAGMA_VIP},
  };

  status = device->DdkAdd(ddk::DeviceAddArgs("as370-nna").set_props(props));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not create as370 nna device: %s", zx_status_get_string(status));
    return status;
  }
  zxlogf(INFO, "Added as370_nna device");

  // intentionally leaked as it is now held by DevMgr.
  __UNUSED auto ptr = device.release();
  return status;
}

void As370NnaDevice::DdkRelease() { delete this; }

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = As370NnaDevice::Create;
  return ops;
}();

}  // namespace as370_nna

// clang-format off
ZIRCON_DRIVER(as370_nna, as370_nna::driver_ops, "zircon", "0.1");
