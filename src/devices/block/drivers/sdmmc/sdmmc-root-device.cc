// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdmmc-root-device.h"

#include <inttypes.h>
#include <lib/ddk/debug.h>
#include <lib/fit/defer.h>
#include <zircon/threads.h>

#include <memory>

#include <fbl/alloc_checker.h>

#include "src/devices/block/drivers/sdmmc/sdmmc-bind.h"

namespace sdmmc {

zx_status_t SdmmcRootDevice::Bind(void* ctx, zx_device_t* parent) {
  ddk::SdmmcProtocolClient host(parent);
  if (!host.is_valid()) {
    zxlogf(ERROR, "failed to get sdmmc protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<SdmmcRootDevice> dev(new (&ac) SdmmcRootDevice(parent, host));

  if (!ac.check()) {
    zxlogf(ERROR, "Failed to allocate device");
    return ZX_ERR_NO_MEMORY;
  }
  zx_status_t st = dev->DdkAdd("sdmmc", DEVICE_ADD_NON_BINDABLE);
  if (st != ZX_OK) {
    return st;
  }

  st = dev->Init();

  __UNUSED auto* placeholder = dev.release();
  return st;
}

zx_status_t SdmmcRootDevice::Init() {
  int rc = thrd_create_with_name(
      &worker_thread_,
      [](void* ctx) -> int { return reinterpret_cast<SdmmcRootDevice*>(ctx)->WorkerThread(); },
      this, "sdmmc-worker");
  if (rc != thrd_success) {
    DdkAsyncRemove();
    return thrd_status_to_zx_status(rc);
  }

  return ZX_OK;
}

// TODO(hanbinyoon): Simplify further using templated lambda come C++20.
template <class DeviceType>
static int MaybeAddDevice(const std::string& name, zx_device_t* zxdev, SdmmcDevice& sdmmc) {
  std::unique_ptr<DeviceType> device;
  if (zx_status_t st = DeviceType::Create(zxdev, sdmmc, &device) != ZX_OK) {
    zxlogf(ERROR, "Failed to create %s device, retcode = %d", name.c_str(), st);
    return thrd_error;
  }

  if (device->Probe() != ZX_OK) {
    return thrd_busy;  // Use this to mean probe failure.
  }

  if (device->AddDevice() != ZX_OK) {
    return thrd_error;
  }

  __UNUSED auto* placeholder = device.release();
  return thrd_success;
}

int SdmmcRootDevice::WorkerThread() {
  auto remove_device_on_error = fit::defer([&]() { DdkAsyncRemove(); });

  SdmmcDevice sdmmc(host_);
  zx_status_t st = sdmmc.Init();
  if (st != ZX_OK) {
    zxlogf(ERROR, "failed to get host info");
    return thrd_error;
  }

  zxlogf(DEBUG, "host caps dma %d 8-bit bus %d max_transfer_size %" PRIu64 "",
         sdmmc.UseDma() ? 1 : 0, (sdmmc.host_info().caps & SDMMC_HOST_CAP_BUS_WIDTH_8) ? 1 : 0,
         sdmmc.host_info().max_transfer_size);

  // Reset the card.
  sdmmc.host().HwReset();

  // No matter what state the card is in, issuing the GO_IDLE_STATE command will
  // put the card into the idle state.
  if ((st = sdmmc.SdmmcGoIdle()) != ZX_OK) {
    zxlogf(ERROR, "SDMMC_GO_IDLE_STATE failed, retcode = %d", st);
    return thrd_error;
  }

  // Probe for SDIO first, then SD/MMC.
  if (auto st = MaybeAddDevice<SdioControllerDevice>("sdio", zxdev(), sdmmc); st != thrd_busy) {
    if (st == thrd_success)
      remove_device_on_error.cancel();
    return st;
  }
  if (auto st = MaybeAddDevice<SdmmcBlockDevice>("block", zxdev(), sdmmc); st != thrd_busy) {
    if (st == thrd_success)
      remove_device_on_error.cancel();
    return st;
  }

  zxlogf(ERROR, "failed to probe");
  return thrd_error;
}

void SdmmcRootDevice::DdkRelease() {
  if (worker_thread_) {
    // Wait until the probe is done.
    thrd_join(worker_thread_, nullptr);
  }

  delete this;
}

}  // namespace sdmmc

static constexpr zx_driver_ops_t sdmmc_driver_ops = []() -> zx_driver_ops_t {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = sdmmc::SdmmcRootDevice::Bind;
  return ops;
}();

ZIRCON_DRIVER(sdmmc, sdmmc_driver_ops, "zircon", "0.1");
