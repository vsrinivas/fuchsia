// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sensor.h"
#include "../global_regs.h"
#include "../pingpong_regs.h"
#include <ddk/debug.h>
#include <ddktl/protocol/ispimpl.h>
#include <zircon/types.h>

namespace camera {

zx_status_t Sensor::HwInit() {
    // Input port safe stop
    InputPort_Config3::Get()
        .ReadFrom(&isp_mmio_)
        .set_mode_request(0)
        .WriteTo(&isp_mmio_);

    zx_status_t status = sensor_callbacks_.SetMode(current_sensor_mode_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Sensor SetMode failed %d\n", __func__, status);
        return status;
    }

    // TODO(braval): disable sensor ISP
    // Reference code makes a call but sensor node
    // has a stub implementation. Keeping this here
    // incase the vendor implements the API

    // If the WDR mode is other than Liner then we need to call
    // an init sequence. Currently the init sequence for linear
    // mode is called in the top level init function. So in case
    // a different mode is added, we need to make sure we call the
    // correct init sequence API. This check is to ensure that when
    // and if a different mode is added, we catch it.
    if (sensor_modes_[current_sensor_mode_].wdr_mode != WDR_MODE_LINEAR) {
        zxlogf(ERROR, "%s: unsupported WDR mode %d\n", __func__, status);
        return ZX_ERR_INVALID_ARGS;
    }

    // TODO(braval): Initialize the calibration data here
    return status;
}

zx_status_t Sensor::SwInit() {
    sensor_info_t info;
    zx_status_t status = GetInfo(&info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Sensor GetInfo failed %d\n", __func__, status);
        return status;
    }

    ping::Top_ActiveDim::Get()
        .ReadFrom(&isp_mmio_local_)
        .set_active_width(info.active.width)
        .set_active_height(info.active.height)
        .WriteTo(&isp_mmio_local_);

    ping::MeteringAf_Active::Get()
        .ReadFrom(&isp_mmio_local_)
        .set_active_width(info.active.width)
        .set_active_height(info.active.height)
        .WriteTo(&isp_mmio_local_);

    ping::Lumvar_ActiveDim::Get()
        .ReadFrom(&isp_mmio_local_)
        .set_active_width(info.active.width)
        .set_active_height(info.active.height)
        .WriteTo(&isp_mmio_local_);

    ping::MeteringAf_Active::Get()
        .ReadFrom(&isp_mmio_local_)
        .set_active_width(info.active.width)
        .set_active_height(info.active.height)
        .WriteTo(&isp_mmio_local_);

    InputPort_HorizontalCrop1::Get()
        .ReadFrom(&isp_mmio_)
        .set_hc_size0(info.active.width)
        .WriteTo(&isp_mmio_);

    InputPort_VerticalCrop0::Get()
        .ReadFrom(&isp_mmio_)
        .set_hc_size1(info.active.width)
        .WriteTo(&isp_mmio_);

    InputPort_VerticalCrop1::Get()
        .ReadFrom(&isp_mmio_)
        .set_vc_size(info.active.height)
        .WriteTo(&isp_mmio_);

    // Input port safe start
    InputPort_Config3::Get()
        .ReadFrom(&isp_mmio_)
        .set_mode_request(1)
        .WriteTo(&isp_mmio_);

    // Update Bayer Bits
    uint8_t isp_bit_width;
    switch (sensor_modes_[current_sensor_mode_].bits) {
    case 8:
        isp_bit_width = 0;
        break;
    case 10:
        isp_bit_width = 1;
        break;
    case 12:
        isp_bit_width = 2;
        break;
    case 14:
        isp_bit_width = 3;
        break;
    case 16:
        isp_bit_width = 4;
        break;
    case 20:
        isp_bit_width = 5;
        break;
    default:
        zxlogf(ERROR, "%s, unsupported  input bits\n", __func__);
        return ZX_ERR_INVALID_ARGS;
        break;
    }

    ping::Top_Config::Get()
        .ReadFrom(&isp_mmio_local_)
        .set_rggb_start_pre_mirror(info.bayer)
        .set_rggb_start_post_mirror(info.bayer)
        .WriteTo(&isp_mmio_local_);

    ping::InputFormatter_Mode::Get()
        .ReadFrom(&isp_mmio_local_)
        .set_input_bitwidth_select(isp_bit_width)
        .WriteTo(&isp_mmio_local_);

    IspGlobal_Config3::Get()
        .ReadFrom(&isp_mmio_)
        .set_mcu_ping_pong_config_select(1)
        .WriteTo(&isp_mmio_);

    return status;
}

zx_status_t Sensor::Init() {

    zx_status_t status = sensor_callbacks_.Init();
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Sensor Init failed %d\n", __func__, status);
        return status;
    }

    size_t actual_modes;
    status = sensor_callbacks_.GetSupportedModes((sensor_mode_t*)(&sensor_modes_),
                                                 kNumModes,
                                                 &actual_modes);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Sensor GetSupportedModes failed %d\n", __func__, status);
        return status;
    }

    if (actual_modes != kNumModes) {
        zxlogf(ERROR, "%s: Num Modes not what expected%d\n", __func__, status);
        return status;
    }

    // Default mode is 0
    status = SetMode(0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Sensor SetMode failed %d\n", __func__, status);
        return status;
    }
    return status;
}

zx_status_t Sensor::SetMode(uint8_t mode) {
    current_sensor_mode_ = mode;

    zx_status_t status = HwInit();
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Sensor HwInit failed %d\n", __func__, status);
        return status;
    }

    status = SwInit();
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Sensor SwInit failed %d\n", __func__, status);
        return status;
    }

    // TODO(braval): Add buffer configuration for temper frames

    return status;
}

zx_status_t Sensor::GetSupportedModes(sensor_mode_t* out_modes_list,
                                      size_t modes_count) {

    if (out_modes_list == nullptr || modes_count != kNumModes) {
        return ZX_ERR_INVALID_ARGS;
    }

    memcpy(out_modes_list, &sensor_modes_, sizeof(sensor_mode_t) * kNumModes);
    return ZX_OK;
}

int32_t Sensor::SetAnalogGain(int32_t gain) {
    return sensor_callbacks_.SetAnalogGain(gain);
}

int32_t Sensor::SetDigitalGain(int32_t gain) {
    return sensor_callbacks_.SetDigitalGain(gain);
}

void Sensor::StartStreaming() {
    sensor_callbacks_.StartStreaming();
}

void Sensor::StopStreaming() {
    sensor_callbacks_.StopStreaming();
}

void Sensor::SetIntegrationTime(int32_t int_time,
                                int32_t int_time_M,
                                int32_t int_time_L) {
    sensor_callbacks_.SetIntegrationTime(int_time, int_time_M, int_time_L);
}

zx_status_t Sensor::Update() {
    return sensor_callbacks_.Update();
}

zx_status_t Sensor::GetInfo(sensor_info_t* out_info) {
    if (out_info == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t status = sensor_callbacks_.GetInfo(out_info);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: Sensor GetInfo failed %d\n", __func__, status);
        return status;
    }

    return ZX_OK;
}

// static
fbl::unique_ptr<Sensor> Sensor::Create(ddk::MmioView isp_mmio,
                                       ddk::MmioView isp_mmio_local,
                                       isp_callbacks_protocol_t sensor_callbacks) {
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
