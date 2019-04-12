// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/power.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>

#include "test.h"

namespace {

static const power_domain_t power_domains[] = {
    { 1 },
    { 3 },
    { 5 },
};

static const pbus_metadata_t power_metadata[] = {
    {
        .type = DEVICE_METADATA_POWER_DOMAINS,
        .data_buffer = &power_domains,
        .data_size = sizeof(power_domains),
    }
};

}

namespace board_test {

zx_status_t TestBoard::PowerInit() {
    pbus_dev_t power_dev = {};
    power_dev.name = "power";
    power_dev.vid = PDEV_VID_TEST;
    power_dev.pid = PDEV_PID_PBUS_TEST;
    power_dev.did = PDEV_DID_TEST_POWER;
    power_dev.metadata_list = power_metadata;
    power_dev.metadata_count = countof(power_metadata);

    zx_status_t status = pbus_.DeviceAdd(&power_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DeviceAdd failed %d\n", __FUNCTION__, status);
        return status;
    }

    return ZX_OK;
}

} // namespace board_test
