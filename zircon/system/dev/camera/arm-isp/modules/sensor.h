// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddktl/protocol/ispimpl.h>
#include <fbl/unique_ptr.h>
#include <lib/mmio/mmio.h>

namespace camera {

namespace {

constexpr uint32_t kNumModes = 3;

}
// This class controls all sensor functionality.
class Sensor {
public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(Sensor);
    Sensor(ddk::MmioView isp_mmio,
           ddk::MmioView isp_mmio_local,
           isp_callbacks_protocol_t sensor_callbacks)
        : isp_mmio_(isp_mmio),
          isp_mmio_local_(isp_mmio_local),
          sensor_callbacks_(&sensor_callbacks) {}

    static fbl::unique_ptr<Sensor> Create(ddk::MmioView isp_mmio,
                                          ddk::MmioView isp_mmio_local,
                                          isp_callbacks_protocol_t sensor_callbacks);
    zx_status_t Init();

    // Sensor APIs for Camera manager to use
    zx_status_t Update();
    zx_status_t SetMode(uint8_t mode);
    zx_status_t GetInfo(sensor_info_t* out_info);
    zx_status_t GetSupportedModes(sensor_mode_t* out_modes_list,
                                  size_t modes_count);
    int32_t SetAnalogGain(int32_t gain);
    int32_t SetDigitalGain(int32_t gain);
    void StartStreaming();
    void StopStreaming();
    void SetIntegrationTime(int32_t int_time,
                            int32_t int_time_M,
                            int32_t int_time_L);

private:
    zx_status_t HwInit();
    zx_status_t SwInit();

    ddk::MmioView isp_mmio_;
    ddk::MmioView isp_mmio_local_;
    ddk::IspCallbacksProtocolClient sensor_callbacks_;

    uint8_t current_sensor_mode_;
    sensor_mode_t sensor_modes_[kNumModes];
};

} // namespace camera
