// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sensor.h"
#include "../global_regs.h"
#include <ddk/debug.h>
#include <zircon/types.h>

namespace camera {

zx_status_t Sensor::Init() {

    zx_status_t status = sensor_callbacks_.Init();
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Sensor Init failed %d\n", __func__, status);
        return status;
    }
    // Input port safe stop
    InputPort_Config3::Get()
        .ReadFrom(&isp_mmio_)
        .set_mode_request(0)
        .WriteTo(&isp_mmio_);

    // Default mode
    status = sensor_callbacks_.SetMode(0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Sensor SetMode failed %d\n", __func__, status);
        return status;
    }

    // TODO(braval): disable sensor ISP
    // Reference code makes a call but sensor node
    // has a stub implementation.

    // TODO(braval): Add remaining init functionality
    // once Sensor driver adds support for it.
    return ZX_OK;
}

fbl::unique_ptr<Sensor> Sensor::Create(ddk::MmioView isp_mmio,
                                       ddk::MmioView isp_mmio_local,
                                       isp_callbacks_t sensor_callbacks) {
    fbl::AllocChecker ac;
    auto sensor = fbl::make_unique_checked<Sensor>(&ac,
                                                   isp_mmio,
                                                   isp_mmio_local,
                                                   std::move(sensor_callbacks));
    if (!ac.check()) {
        return nullptr;
    }

    zx_status_t status = sensor->Init();
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Sensor Init failed %d\n", __func__, status);
        return nullptr;
    }

    return sensor;
}

} // namespace camera
