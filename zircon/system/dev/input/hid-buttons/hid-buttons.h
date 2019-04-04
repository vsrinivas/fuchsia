// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/metadata/buttons.h>
#include <ddk/protocol/gpio.h>

#include <ddktl/device.h>
#include <ddktl/protocol/hidbus.h>

#include <fbl/array.h>
#include <fbl/mutex.h>

#include <hid/buttons.h>

#include <lib/zx/interrupt.h>
#include <lib/zx/port.h>

#include <zircon/thread_annotations.h>

#include <optional>

namespace buttons {

class HidButtonsDevice;
using DeviceType = ddk::Device<HidButtonsDevice, ddk::Unbindable>;

class HidButtonsDevice : public DeviceType,
                         public ddk::HidbusProtocol<HidButtonsDevice, ddk::base_protocol> {
public:
    explicit HidButtonsDevice(zx_device_t* device)
        : DeviceType(device) {}

    zx_status_t Bind();

    // Methods required by the ddk mixins.
    zx_status_t HidbusStart(const hidbus_ifc_protocol_t* ifc) TA_EXCL(client_lock_);
    zx_status_t HidbusQuery(uint32_t options, hid_info_t* info);
    void HidbusStop() TA_EXCL(client_lock_);
    zx_status_t HidbusGetDescriptor(uint8_t desc_type, void** data, size_t* len);
    zx_status_t HidbusGetReport(uint8_t rpt_type, uint8_t rpt_id, void* data,
                                size_t len, size_t* out_len) TA_EXCL(client_lock_);
    zx_status_t HidbusSetReport(uint8_t rpt_type, uint8_t rpt_id, const void* data, size_t len);
    zx_status_t HidbusGetIdle(uint8_t rpt_id, uint8_t* duration);
    zx_status_t HidbusSetIdle(uint8_t rpt_id, uint8_t duration);
    zx_status_t HidbusGetProtocol(uint8_t* protocol);
    zx_status_t HidbusSetProtocol(uint8_t protocol);

    void DdkUnbind();
    void DdkRelease();

private:
    struct Gpio {
        gpio_protocol_t gpio;
        zx::interrupt irq;
        buttons_gpio_config_t config;
    };

    int Thread();
    void ShutDown() TA_EXCL(client_lock_);
    void ReconfigurePolarity(uint32_t idx, uint64_t int_port);
    zx_status_t ConfigureInterrupt(uint32_t idx, uint64_t int_port);
    bool MatrixScan(uint32_t row, uint32_t col, zx_duration_t delay);

    thrd_t thread_;
    zx::port port_;
    fbl::Mutex client_lock_;
    ddk::HidbusIfcProtocolClient client_ TA_GUARDED(client_lock_);
    fbl::Array<buttons_button_config_t> buttons_;
    fbl::Array<Gpio> gpios_;
    std::optional<uint8_t> fdr_gpio_;
};
}
