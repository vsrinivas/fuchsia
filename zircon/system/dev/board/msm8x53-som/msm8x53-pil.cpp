// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>
#include <zircon/syscalls/smc.h>

#include "msm8x53.h"

namespace board_msm8x53 {

zx_status_t Msm8x53::PilInit() {
    constexpr pbus_smc_t smcs[] = {
        {
            .service_call_num_base = ARM_SMC_SERVICE_CALL_NUM_SIP_SERVICE_BASE,
            .count = ARM_SMC_SERVICE_CALL_NUM_SIP_SERVICE_LENGTH,
        }};

    pbus_dev_t dev = {};
    dev.name = "msm8x53-pil";
    dev.vid = PDEV_VID_QUALCOMM;
    dev.did = PDEV_DID_QUALCOMM_PIL;
    dev.smc_list = smcs;
    dev.smc_count = countof(smcs);

    zx_status_t status;
    if ((status = pbus_.DeviceAdd(&dev)) != ZX_OK) {
        zxlogf(ERROR, "%s: Could not add dev %d\n", __func__, status);
        return status;
    }

    return ZX_OK;
}

} // namespace board_msm8x53
