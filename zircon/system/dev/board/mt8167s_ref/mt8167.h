// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/protocol/gpioimpl.h>

#include <ddktl/device.h>
#include <ddktl/protocol/platform/bus.h>

#include <fbl/macros.h>

#include <threads.h>

#include <ddk/usb-peripheral-config.h>
#include <soc/mt8167/mt8167-power.h>

namespace board_mt8167 {

// BTI IDs for our devices
enum {
    BTI_BOARD,
    BTI_DISPLAY,
    BTI_MSDC0,
    BTI_MSDC1,
    BTI_MSDC2,
    BTI_USB,
    BTI_AUDIO_OUT,
    BTI_AUDIO_IN,
    BTI_SYSMEM,
    BTI_GPU,
};

class Mt8167;
using Mt8167Type = ddk::Device<Mt8167>;

// This is the main class for the platform bus driver.
class Mt8167 : public Mt8167Type {
public:
    explicit Mt8167(zx_device_t* parent, pbus_protocol_t* pbus, pdev_board_info_t* board_info)
        : Mt8167Type(parent), pbus_(pbus), board_info_(*board_info) {}

    static zx_status_t Create(zx_device_t* parent);

    // Device protocol implementation.
    void DdkRelease();

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Mt8167);

    zx_status_t Start();
    zx_status_t Msdc0Init();
    zx_status_t Msdc1Init();
    zx_status_t Msdc2Init();
    zx_status_t SocInit();
    zx_status_t SysmemInit();
    zx_status_t GpioInit();
    zx_status_t GpuInit();
    zx_status_t DisplayInit();
    zx_status_t I2cInit();
    zx_status_t ButtonsInit();
    zx_status_t ClkInit();
    zx_status_t PowerInit();
    zx_status_t UsbInit();
    zx_status_t ThermalInit();
    zx_status_t TouchInit();
    zx_status_t SensorsInit();
    zx_status_t BacklightInit();
    zx_status_t AudioInit();
    int Thread();

    ddk::PBusProtocolClient pbus_;
    gpio_impl_protocol_t gpio_impl_;
    pdev_board_info_t board_info_;
    thrd_t thread_;
    UsbConfig* usb_config_;
};

} // namespace board_mt8167
