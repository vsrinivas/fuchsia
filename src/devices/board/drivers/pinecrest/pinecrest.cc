// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pinecrest.h"

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/platform-defs.h>
#include <zircon/status.h>
#include <zircon/threads.h>

#include <fbl/alloc_checker.h>

#include "src/devices/board/drivers/pinecrest/pinecrest-bind.h"

namespace board_pinecrest {
namespace fpbus = fuchsia_hardware_platform_bus;

zx_status_t Pinecrest::Create(void* ctx, zx_device_t* parent) {
  auto endpoints = fdf::CreateEndpoints<fpbus::PlatformBus>();
  if (endpoints.is_error()) {
    return endpoints.error_value();
  }

  zx_status_t status = device_connect_runtime_protocol(
      parent, fpbus::Service::PlatformBus::ServiceName, fpbus::Service::PlatformBus::Name,
      endpoints->server.TakeHandle().release());
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to connect to platform bus: %s", zx_status_get_string(status));
    return status;
  }

  fdf::WireSyncClient<fpbus::PlatformBus> pbus(std::move(endpoints->client));
  auto result = pbus.buffer(fdf::Arena('INFO'))->GetBoardInfo();
  if (!result.ok()) {
    zxlogf(ERROR, "%s: GetBoardInfo request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: GetBoardInfo failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  fbl::AllocChecker ac;
  auto board = fbl::make_unique_checked<Pinecrest>(&ac, parent, pbus.TakeClientEnd(),
                                                   fidl::ToNatural(result->value()->info));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = board->DdkAdd("pinecrest", DEVICE_ADD_NON_BINDABLE)) != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed %s", __func__, zx_status_get_string(status));
    return status;
  }

  if ((status = board->Start()) != ZX_OK) {
    return status;
  }

  __UNUSED auto* dummy = board.release();
  return ZX_OK;
}

zx_status_t Pinecrest::Start() {
  auto cb = [](void* arg) -> int { return reinterpret_cast<Pinecrest*>(arg)->Thread(); };
  int rc = thrd_create_with_name(&thread_, cb, this, "pinecrest-start-thread");
  return thrd_status_to_zx_status(rc);
}

int Pinecrest::Thread() {
  auto status = GpioInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: GpioInit() failed: %s", __func__, zx_status_get_string(status));
    return thrd_error;
  }

  status = ClockInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: ClkInit() failed: %s", __func__, zx_status_get_string(status));
    return thrd_error;
  }

  status = I2cInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: I2cInit() failed: %s", __func__, zx_status_get_string(status));
    return thrd_error;
  }

  status = RegistersInit();
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: RegistersInit() failed: %s", __func__, zx_status_get_string(status));
    return thrd_error;
  }

  if (UsbInit() != ZX_OK) {
    zxlogf(ERROR, "%s: UsbInit() failed", __func__);
  }

  if (AudioInit() != ZX_OK) {
    zxlogf(ERROR, "%s: AudioInit() failed", __func__);
    // In case of error report it and keep going.
  }

  if (LightInit() != ZX_OK) {
    zxlogf(ERROR, "%s: LightInit() failed", __func__);
  }

  if (TouchInit() != ZX_OK) {
    zxlogf(ERROR, "%s: TouchInit() failed", __func__);
  }

  if (NandInit() != ZX_OK) {
    zxlogf(ERROR, "%s: NandInit() failed", __func__);
  }

  if (NnaInit() != ZX_OK) {
    zxlogf(ERROR, "%s: NnaInit() failed", __func__);
  }

  if (PowerInit() != ZX_OK) {
    zxlogf(ERROR, "%s: PowerInit() failed", __func__);
    // In case of error report it and keep going.
  }

  if (ThermalInit() != ZX_OK) {
    zxlogf(ERROR, "%s: ThermalInit() failed", __func__);
  }

  if (SdioInit() != ZX_OK) {
    zxlogf(ERROR, "%s: SdioInit() failed", __func__);
  }

  return 0;
}
}  // namespace board_pinecrest

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = board_pinecrest::Pinecrest::Create;
  return ops;
}();

ZIRCON_DRIVER(as370, driver_ops, "zircon", "0.1");
