// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/metadata/buttons.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform/device.h>
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

// zx_port_packet::key.
constexpr uint64_t kPortKeyShutDown = 0x01;
// Start of up to kNumberOfRequiredGpios port types used for interrupts.
constexpr uint64_t kPortKeyInterruptStart = 0x10;

class HidButtonsDevice;
using DeviceType = ddk::Device<HidButtonsDevice, ddk::Unbindable>;

class HidButtonsDevice : public DeviceType,
                         public ddk::HidbusProtocol<HidButtonsDevice, ddk::base_protocol> {
public:
    explicit HidButtonsDevice(zx_device_t* device)
        : DeviceType(device) {}
    virtual ~HidButtonsDevice() = default;

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

protected:
    // Protected for unit testing.
    zx::port port_;
    void ShutDown() TA_EXCL(client_lock_);

private:
    struct Gpio {
        gpio_protocol_t gpio;
        zx::interrupt irq;
        buttons_gpio_config_t config;
    };

    int Thread();
    void ReconfigurePolarity(uint32_t idx, uint64_t int_port);
    zx_status_t ConfigureInterrupt(uint32_t idx, uint64_t int_port);
    bool MatrixScan(uint32_t row, uint32_t col, zx_duration_t delay);
    // To be overwritten in unit testing.
    virtual zx_status_t PdevGetGpioProtocol(const pdev_protocol_t* proto, uint32_t index,
                                            void* out_protocol_buffer, size_t out_protocol_size,
                                            size_t* out_protocol_actual) {
        return pdev_get_protocol(proto, ZX_PROTOCOL_GPIO, index, out_protocol_buffer,
                                 out_protocol_size, out_protocol_actual);
    }

    thrd_t thread_;
    fbl::Mutex client_lock_;
    ddk::HidbusIfcProtocolClient client_ TA_GUARDED(client_lock_);
    fbl::Array<buttons_button_config_t> buttons_;
    fbl::Array<Gpio> gpios_;
    std::optional<uint8_t> fdr_gpio_;
};
} // namespace buttons
