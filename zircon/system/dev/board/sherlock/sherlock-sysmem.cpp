// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>

#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"

namespace sherlock {

static const pbus_bti_t sysmem_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_SYSMEM,
    },
};

static const pbus_dev_t sysmem_dev = []{
    pbus_dev_t ret;
    ret.name = "sysmem";
    ret.vid = PDEV_VID_GENERIC;
    ret.pid = PDEV_PID_GENERIC;
    ret.did = PDEV_DID_SYSMEM;
    ret.bti_list = sysmem_btis;
    ret.bti_count = countof(sysmem_btis);
    return ret;
}();

zx_status_t Sherlock::SysmemInit() {
    zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_SYSMEM, &sysmem_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ProtocolDeviceAdd failed %d\n", __func__, status);
        return status;
    }

    return ZX_OK;
}

} // namespace sherlock
