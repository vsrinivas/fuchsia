// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/gpio.h>

#include <ddktl/device.h>
#include <ddktl/protocol/hidbus.h>

#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>

#include <hid/buttons.h>

#include <lib/zx/interrupt.h>

#include <zircon/thread_annotations.h>

namespace buttons {

class HidButtonsDevice;
using DeviceType = ddk::Device<HidButtonsDevice, ddk::Unbindable>;

class HidButtonsDevice : public DeviceType,
                         public ddk::HidBusProtocol<HidButtonsDevice> {
public:
    explicit HidButtonsDevice(zx_device_t* device)
        : DeviceType(device) {}

    zx_status_t Bind();

    // Methods required by the ddk mixins.
    zx_status_t HidBusStart(ddk::HidBusIfcProxy proxy) TA_EXCL(proxy_lock_);
    zx_status_t HidBusQuery(uint32_t options, hid_info_t* info);
    void HidBusStop() TA_EXCL(proxy_lock_);
    zx_status_t HidBusGetDescriptor(uint8_t desc_type, void** data, size_t* len);
    zx_status_t HidBusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data,
                                size_t len, size_t* out_len) TA_EXCL(proxy_lock_);
    zx_status_t HidBusSetReport(uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len);
    zx_status_t HidBusGetIdle(uint8_t rpt_id, uint8_t* duration);
    zx_status_t HidBusSetIdle(uint8_t rpt_id, uint8_t duration);
    zx_status_t HidBusGetProtocol(uint8_t* protocol);
    zx_status_t HidBusSetProtocol(uint8_t protocol);

    void DdkUnbind();
    void DdkRelease();

private:
    enum {
        kGpioVolumeUp = 0,
        kGpioVolumeDown,
        kGpioVolumeUpDown,
        kNumberOfRequiredGpios,
    };
    struct GpioKeys {
        gpio_protocol_t gpio;
        zx::interrupt irq;
    };
    fbl::unique_ptr<GpioKeys[]> keys_;

    int Thread();
    void ShutDown() TA_EXCL(proxy_lock_);
    zx_status_t ConfigureGpio(uint32_t idx, uint64_t int_port);

    thrd_t thread_;
    zx_handle_t port_handle_;
    fbl::Mutex proxy_lock_;
    ddk::HidBusIfcProxy proxy_ TA_GUARDED(proxy_lock_);
};
}
