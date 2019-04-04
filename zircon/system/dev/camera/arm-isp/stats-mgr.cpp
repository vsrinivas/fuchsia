// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "stats-mgr.h"

namespace camera {

fbl::unique_ptr<StatsManager> StatsManager::Create(ddk::MmioView isp_mmio,
                                                   ddk::MmioView isp_mmio_local,
                                                   isp_callbacks_protocol_t sensor_callbacks) {
    // First initialize all the modules
    fbl::AllocChecker ac;
    auto sensor = camera::Sensor::Create(isp_mmio, isp_mmio_local, sensor_callbacks);
    if (sensor == nullptr) {
        zxlogf(ERROR, "%s: Unable to start Sensor Module \n", __func__);
        return nullptr;
    }

    // Once all modules are initialized, create the StatsManger instance
    auto statsmanager = fbl::make_unique_checked<StatsManager>(&ac, std::move(sensor));
    if (!ac.check()) {
        zxlogf(ERROR, "%s: Unable to start StatsManager \n", __func__);
        return nullptr;
    }
    return statsmanager;
}

} // namespace camera
