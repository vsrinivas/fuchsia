// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "imx8mmevk.h"

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform/bus.h>
#include <ddktl/protocol/gpioimpl.h>
#include <soc/imx8m-mini/imx8m-mini-hw.h>
#include <soc/imx8m-mini/imx8m-mini-iomux.h>

namespace imx8mmevk {

namespace {

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

} // namespace

zx_status_t Board::StartSysmem() {
    auto status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_SYSMEM, &sysmem_dev);
    if (status != ZX_OK) {
        ERROR("ProtocolDeviceAdd() error: %d\n", status);
        return status;
    }

    return ZX_OK;
}

} // namespace imx8mmevk
