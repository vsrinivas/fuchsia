// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vim.h"
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/bus.h>

namespace vim {
static const pbus_bti_t sysmem_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_SYSMEM,
    },
};

static const pbus_dev_t sysmem_dev = []() {
    pbus_dev_t dev;
    dev.name = "sysmem";
    dev.vid = PDEV_VID_GENERIC;
    dev.pid = PDEV_PID_GENERIC;
    dev.did = PDEV_DID_SYSMEM;
    dev.bti_list = sysmem_btis;
    dev.bti_count = countof(sysmem_btis);
    return dev;
}();

zx_status_t Vim::SysmemInit() {
    zx_status_t status;

    if ((status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_SYSMEM, &sysmem_dev)) != ZX_OK) {
        zxlogf(ERROR, "SysmemInit: pbus_protocol_device_add() failed for sysmem: %d\n", status);
        return status;
    }

    return ZX_OK;
}
} //namespace vim
