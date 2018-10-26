// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/gpio.h>

#include <ddktl/device.h>
#include <ddktl/protocol/hidbus.h>

#include <fbl/array.h>
#include <fbl/mutex.h>

#include <hid/buttons.h>

#include <lib/zx/interrupt.h>

#include <zircon/thread_annotations.h>

namespace buttons {

class HidButtonsDevice;
using DeviceType = ddk::Device<HidButtonsDevice, ddk::Unbindable>;

class HidButtonsDevice : public DeviceType,
                         public ddk::HidbusProtocol<HidButtonsDevice> {
public:
    explicit HidButtonsDevice(zx_device_t* device)
        : DeviceType(device) {}

    zx_status_t Bind();

    // Methods required by the ddk mixins.
    zx_status_t HidbusStart(const hidbus_ifc_t* ifc) TA_EXCL(proxy_lock_);
    zx_status_t HidbusQuery(uint32_t options, hid_info_t* info);
    void HidbusStop() TA_EXCL(proxy_lock_);
    zx_status_t HidbusGetDescriptor(uint8_t desc_type, void** data, size_t* len);
    zx_status_t HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data,
                                size_t len, size_t* out_len) TA_EXCL(proxy_lock_);
    zx_status_t HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const void* data, size_t len);
    zx_status_t HidbusGetIdle(uint8_t rpt_id, uint8_t* duration);
    zx_status_t HidbusSetIdle(uint8_t rpt_id, uint8_t duration);
    zx_status_t HidbusGetProtocol(uint8_t* protocol);
    zx_status_t HidbusSetProtocol(uint8_t protocol);

    void DdkUnbind();
    void DdkRelease();

private:
    enum {
        kGpioVolumeUp = 0,
        kGpioVolumeDown,
        kGpioVolumeUpDown,
        kGpioMicPrivacy,
        kNumberOfRequiredGpios,
    };
    struct GpioKeys {
        gpio_protocol_t gpio;
        zx::interrupt irq;
    };

    int Thread();
    void ShutDown() TA_EXCL(proxy_lock_);
    zx_status_t ReconfigureGpio(uint32_t idx, uint64_t int_port);

    thrd_t thread_;
    zx_handle_t port_handle_;
    fbl::Mutex proxy_lock_;
    ddk::HidbusIfcProxy proxy_ TA_GUARDED(proxy_lock_);
    fbl::Array<GpioKeys> keys_;
};
}
