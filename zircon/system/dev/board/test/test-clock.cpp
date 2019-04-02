// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/clock.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>

#include "test.h"

namespace board_test {

namespace {

static const uint32_t clock_id_maps[] = {
    2, /* map_count */
    3, /* clock_count */
    2, 3, 4, /* clock_ids */
    4, /* clock_count */
    5, 6, 7, 8, /* clock_ids */
};

static const pbus_metadata_t gpio_metadata[] = {
    {
        .type = DEVICE_METADATA_CLOCK_MAPS,
        .data_buffer = &clock_id_maps,
        .data_size = sizeof(clock_id_maps),
    }
};

}

zx_status_t TestBoard::ClockInit() {
    pbus_dev_t clock_dev = {};
    clock_dev.name = "clock";
    clock_dev.vid = PDEV_VID_TEST;
    clock_dev.pid = PDEV_PID_PBUS_TEST;
    clock_dev.did = PDEV_DID_TEST_CLOCK;
    clock_dev.metadata_list = gpio_metadata;
    clock_dev.metadata_count = countof(gpio_metadata);

    zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_CLOCK_IMPL, &clock_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ProtocolDeviceAdd failed %d\n", __FUNCTION__, status);
        return status;
    }

    return ZX_OK;
}

} // namespace board_test
