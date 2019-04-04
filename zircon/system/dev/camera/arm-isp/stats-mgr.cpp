// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "stats-mgr.h"

namespace camera {

int StatsManager::FrameProcessingThread() {
    zxlogf(INFO, "%s start\n", __func__);

    while (running_.load()) {
        sync_completion_wait(&frame_processing_signal_, ZX_TIME_INFINITE);

        // TODO(braval): Start processing the frame here

        // Reset the signal
        sync_completion_reset(&frame_processing_signal_);
    }

    return ZX_OK;
}

fbl::unique_ptr<StatsManager> StatsManager::Create(ddk::MmioView isp_mmio,
                                                   ddk::MmioView isp_mmio_local,
                                                   isp_callbacks_protocol_t sensor_callbacks,
                                                   sync_completion_t frame_processing_signal) {
    // First initialize all the modules
    fbl::AllocChecker ac;
    auto sensor = camera::Sensor::Create(isp_mmio, isp_mmio_local, sensor_callbacks);
    if (sensor == nullptr) {
        zxlogf(ERROR, "%s: Unable to start Sensor Module \n", __func__);
        return nullptr;
    }

    auto worker_thunk = [](void* arg) -> int {
        return reinterpret_cast<StatsManager*>(arg)->FrameProcessingThread();
    };

    // Once all modules are initialized, create the StatsManger instance
    auto statsmanager = fbl::make_unique_checked<StatsManager>(&ac,
                                                               std::move(sensor),
                                                               frame_processing_signal);
    if (!ac.check()) {
        zxlogf(ERROR, "%s: Unable to start StatsManager \n", __func__);
        return nullptr;
    }

    int ret = thrd_create_with_name(&statsmanager->frame_processing_thread_,
                                    worker_thunk,
                                    reinterpret_cast<void*>(statsmanager.get()),
                                    "frame_processing thread");
    ZX_DEBUG_ASSERT(ret == thrd_success);

    statsmanager->running_.store(true);

    return statsmanager;
}

StatsManager::~StatsManager() {
    running_.store(false);
    thrd_join(frame_processing_thread_, NULL);
}

} // namespace camera
