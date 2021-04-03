// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdmmc-root-device.h"

#include <inttypes.h>
#include <lib/ddk/debug.h>
#include <lib/fit/defer.h>
#include <zircon/threads.h>

#include <memory>

#include "src/devices/block/drivers/sdmmc/sdmmc-bind.h"

namespace sdmmc {

zx_status_t SdmmcRootDevice::Bind(void* ctx, zx_device_t* parent) {
  ddk::SdmmcProtocolClient host(parent);
  if (!host.is_valid()) {
    zxlogf(ERROR, "sdmmc: failed to get sdmmc protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<SdmmcRootDevice> dev(new (&ac) SdmmcRootDevice(parent, host));

  if (!ac.check()) {
    zxlogf(ERROR, "SdmmcRootDevice:Bind: Failed to allocate device");
    return ZX_ERR_NO_MEMORY;
  }
  zx_status_t st = dev->DdkAdd("sdmmc", DEVICE_ADD_NON_BINDABLE);
  if (st != ZX_OK) {
    return st;
  }

  st = dev->Init();

  __UNUSED auto* dummy = dev.release();
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

int SdmmcRootDevice::WorkerThread() {
  auto remove_device_on_error = fit::defer([&]() { DdkAsyncRemove(); });

  SdmmcDevice sdmmc(host_);
  zx_status_t st = sdmmc.Init();
  if (st != ZX_OK) {
    zxlogf(ERROR, "sdmmc: failed to get host info");
    return thrd_error;
  }

  zxlogf(DEBUG, "sdmmc: host caps dma %d 8-bit bus %d max_transfer_size %" PRIu64 "",
         sdmmc.UseDma() ? 1 : 0, (sdmmc.host_info().caps & SDMMC_HOST_CAP_BUS_WIDTH_8) ? 1 : 0,
         sdmmc.host_info().max_transfer_size);

  // Reset the card.
  sdmmc.host().HwReset();

  std::unique_ptr<SdmmcBlockDevice> block_dev;
  if ((st = SdmmcBlockDevice::Create(zxdev(), sdmmc, &block_dev)) != ZX_OK) {
    zxlogf(ERROR, "sdmmc: Failed to create block device, retcode = %d", st);
    return thrd_error;
  }

  std::unique_ptr<SdioControllerDevice> sdio_dev;
  if ((st = SdioControllerDevice::Create(zxdev(), sdmmc, &sdio_dev)) != ZX_OK) {
    zxlogf(ERROR, "sdmmc: Failed to create block device, retcode = %d", st);
    return thrd_error;
  }

  // No matter what state the card is in, issuing the GO_IDLE_STATE command will
  // put the card into the idle state.
  if ((st = sdmmc.SdmmcGoIdle()) != ZX_OK) {
    zxlogf(ERROR, "sdmmc: SDMMC_GO_IDLE_STATE failed, retcode = %d", st);
    return thrd_error;
  }

  // Probe for SDIO, SD and then MMC
  if ((st = sdio_dev->ProbeSdio()) == ZX_OK) {
    if ((st = sdio_dev->AddDevice()) == ZX_OK) {
      __UNUSED auto* dummy = sdio_dev.release();
      remove_device_on_error.cancel();
      return thrd_success;
    }

    return thrd_error;
  } else if ((st = block_dev->ProbeSd()) != ZX_OK && (st = block_dev->ProbeMmc()) != ZX_OK) {
    zxlogf(ERROR, "sdmmc: failed to probe");
    return thrd_error;
  }

  if ((st = block_dev->AddDevice()) != ZX_OK) {
    return thrd_error;
  }

  __UNUSED auto* dummy = block_dev.release();
  remove_device_on_error.cancel();
  return thrd_success;
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
