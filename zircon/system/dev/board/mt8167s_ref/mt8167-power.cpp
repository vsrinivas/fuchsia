// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <soc/mt8167/mt8167-hw.h>

#include "mt8167.h"

namespace board_mt8167 {

zx_status_t Mt8167::PowerInit() {
    static const pbus_mmio_t power_mmios[] = {
        {
            .base = MT8167_PMIC_WRAP_BASE,
            .length = MT8167_PMIC_WRAP_SIZE,
        }
    };

    pbus_dev_t power_dev = {};
    power_dev.name = "power";
    power_dev.vid = PDEV_VID_MEDIATEK;
    power_dev.did = PDEV_DID_MEDIATEK_POWER;
    power_dev.mmio_list = power_mmios;
    power_dev.mmio_count = countof(power_mmios);

    zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_POWER_IMPL, &power_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Adding power device failed %d\n", __FUNCTION__, status);
    }

    return status;
}

}  // namespace board_mt8167
