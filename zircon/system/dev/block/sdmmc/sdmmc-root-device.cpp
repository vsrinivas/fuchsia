// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdmmc-root-device.h"

#include <inttypes.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <zircon/threads.h>

#include "sdio-device.h"
#include "sdmmc-block-device.h"

namespace sdmmc {

zx_status_t SdmmcRootDevice::Bind(void* ctx, zx_device_t* parent) {
    ddk::SdmmcProtocolClient host(parent);
    if (!host.is_valid()) {
        zxlogf(ERROR, "sdmmc: failed to get sdmmc protocol\n");
        return ZX_ERR_NOT_SUPPORTED;
    }

    fbl::AllocChecker ac;
    fbl::unique_ptr<SdmmcRootDevice> dev(new (&ac) SdmmcRootDevice(parent, host));

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
        zx_status_t st = thrd_status_to_zx_status(rc);
        if (!dead_) {
            DdkRemove();
        }
        return st;
    }

    return ZX_OK;
}

int SdmmcRootDevice::WorkerThread() {
    sdmmc_host_info_t host_info;
    zx_status_t st = host_.HostInfo(&host_info);
    if (st != ZX_OK) {
        zxlogf(ERROR, "sdmmc: failed to get host info\n");
        if (!dead_) {
            DdkRemove();
        }
        return thrd_error;
    }

    SdmmcDevice sdmmc(host_, host_info);

    zxlogf(TRACE, "sdmmc: host caps dma %d 8-bit bus %d max_transfer_size %" PRIu64 "\n",
           sdmmc.UseDma() ? 1 : 0, (sdmmc.host_info().caps & SDMMC_HOST_CAP_BUS_WIDTH_8) ? 1 : 0,
           sdmmc.host_info().max_transfer_size);

    // Reset the card.
    sdmmc.host().HwReset();

    if ((st = SdmmcBlockDevice::Create(zxdev(), sdmmc, &block_dev_)) != ZX_OK) {
        zxlogf(ERROR, "sdmmc: Failed to create block device, retcode = %d\n", st);
        if (!dead_) {
            DdkRemove();
        }
        return thrd_error;
    }

    if ((st = SdioDevice::Create(zxdev(), sdmmc, &sdio_dev_)) != ZX_OK) {
        zxlogf(ERROR, "sdmmc: Failed to create block device, retcode = %d\n", st);
        if (!dead_) {
            DdkRemove();
        }
        return thrd_error;
    }

    // No matter what state the card is in, issuing the GO_IDLE_STATE command will
    // put the card into the idle state.
    if ((st = sdmmc.SdmmcGoIdle()) != ZX_OK) {
        zxlogf(ERROR, "sdmmc: SDMMC_GO_IDLE_STATE failed, retcode = %d\n", st);
        if (!dead_) {
            DdkRemove();
        }
        return thrd_error;
    }

    // Probe for SDIO, SD and then MMC
    if ((st = sdio_dev_->ProbeSdio()) == ZX_OK) {
        if ((st = sdio_dev_->AddDevice()) == ZX_OK) {
            return thrd_success;
        }

        if (!dead_) {
            DdkRemove();
        }
        return thrd_error;
    } else if ((st = block_dev_->ProbeSd()) != ZX_OK && (st = block_dev_->ProbeMmc()) != ZX_OK) {
        zxlogf(ERROR, "sdmmc: failed to probe\n");
        if (!dead_) {
            DdkRemove();
        }
        return thrd_error;
    }

    if ((st = block_dev_->AddDevice()) == ZX_OK) {
        return thrd_success;
    }

    if (!dead_) {
        DdkRemove();
    }
    return thrd_error;
}

void SdmmcRootDevice::DdkUnbind() {
    if (dead_) {
        // Already in middle of release.
        return;
    }

    dead_ = true;

    if (block_dev_) {
        block_dev_->DdkRemove();
    }
    if (sdio_dev_) {
        sdio_dev_->DdkRemove();
    }

    DdkRemove();
}

void SdmmcRootDevice::DdkRelease() {
    dead_ = true;

    if (worker_thread_) {
        // Wait until the probe is done.
        thrd_join(worker_thread_, nullptr);
    }

    delete this;
}

}  // namespace sdmmc

static zx_driver_ops_t sdmmc_driver_ops = []() -> zx_driver_ops_t {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = sdmmc::SdmmcRootDevice::Bind;
    return ops;
}();

// The formatter does not play nice with these macros.
// clang-format off
ZIRCON_DRIVER_BEGIN(sdmmc, sdmmc_driver_ops, "zircon", "0.1", 1)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SDMMC),
ZIRCON_DRIVER_END(sdmmc)
    // clang-format on
