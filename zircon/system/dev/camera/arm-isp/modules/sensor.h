// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddktl/protocol/ispimpl.h>
#include <fbl/unique_ptr.h>
#include <lib/mmio/mmio.h>

namespace camera {

// This class controls all sensor functionality.
class Sensor {
public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(Sensor);
    Sensor(ddk::MmioView isp_mmio,
           ddk::MmioView isp_mmio_local,
           isp_callbacks_t sensor_callbacks)
        : isp_mmio_(isp_mmio),
          isp_mmio_local_(isp_mmio_local),
          sensor_callbacks_(&sensor_callbacks) {}

    static fbl::unique_ptr<Sensor> Create(ddk::MmioView isp_mmio,
                                          ddk::MmioView isp_mmio_local,
                                          isp_callbacks_t sensor_callbacks);
    zx_status_t Init();

private:
    ddk::MmioView isp_mmio_;
    ddk::MmioView isp_mmio_local_;
    ddk::IspCallbacksClient sensor_callbacks_;
};

} // namespace camera
