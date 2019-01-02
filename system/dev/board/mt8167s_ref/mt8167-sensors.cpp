// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>

#include "mt8167.h"

namespace board_mt8167 {

zx_status_t Mt8167::SensorsInit() {
    // Lite-On LTR-578ALS proximity/ambient light sensor.

    pdev_board_info_t info;
    zx_status_t status = pbus_.GetBoardInfo(&info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: GetBoardInfo failed\n", __FILE__);
        return status;
    }

    if (info.vid != PDEV_VID_GOOGLE || info.pid != PDEV_PID_CLEO) {
        return ZX_OK;
    }

    static constexpr pbus_i2c_channel_t ltr_578als_i2cs[] = {
        {
            .bus_id = 0,
            .address = 0x53
        },
    };

    pbus_dev_t ltr_578als_dev = {};
    ltr_578als_dev.name = "ltr-578als";
    ltr_578als_dev.vid = PDEV_VID_GENERIC;
    ltr_578als_dev.did = PDEV_DID_LITE_ON_ALS;
    ltr_578als_dev.i2c_channel_list = ltr_578als_i2cs;
    ltr_578als_dev.i2c_channel_count = countof(ltr_578als_i2cs);

    if ((status = pbus_.DeviceAdd(&ltr_578als_dev)) != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to add LTR-578ALS device: %d\n", __FUNCTION__, status);
    }

    // TODO(bradenkell): Add BMA253 acceleration sensor.

    return status;
}

}  // namespace board_mt8167
