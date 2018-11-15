// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>

extern zx_status_t mtk_sdmmc_bind(void* ctx, zx_device_t* parent);

static zx_driver_ops_t mtk_sdmmc_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = mtk_sdmmc_bind,
};

ZIRCON_DRIVER_BEGIN(mtk_sdmmc, mtk_sdmmc_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_MEDIATEK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_MEDIATEK_EMMC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_MEDIATEK_SDIO),
ZIRCON_DRIVER_END(mtk_sdmmc)
