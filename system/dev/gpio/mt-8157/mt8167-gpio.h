// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/platform-defs.h>
#include <ddk/protocol/gpio-impl.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-device.h>

#include <ddktl/device.h>
#include <ddktl/protocol/gpio-impl.h>

#include <hw/reg.h>
#include <hwreg/mmio.h>

namespace gpio {

class Mt8167GpioDevice;
using DeviceType = ddk::Device<Mt8167GpioDevice, ddk::Unbindable>;

class Mt8167GpioDevice : public DeviceType,
                         public ddk::GpioImplProtocol<Mt8167GpioDevice> {
public:
    Mt8167GpioDevice(zx_device_t* parent)
        : DeviceType(parent) {}

    zx_status_t Bind();

    // Methods required by the ddk mixins
    void DdkUnbind();
    void DdkRelease();

    zx_status_t GpioImplConfigIn(uint32_t index, uint32_t flags);
    zx_status_t GpioImplConfigOut(uint32_t index, uint8_t initial_value);
    zx_status_t GpioImplSetAltFunction(uint32_t index, uint64_t function);
    zx_status_t GpioImplRead(uint32_t index, uint8_t* out_value);
    zx_status_t GpioImplWrite(uint32_t index, uint8_t value);
    zx_status_t GpioImplGetInterrupt(uint32_t index, uint32_t flags, zx_handle_t* out_handle);
    zx_status_t GpioImplReleaseInterrupt(uint32_t index);
    zx_status_t GpioImplSetPolarity(uint32_t index, uint32_t polarity);

private:
    platform_device_protocol_t pdev_;
    platform_bus_protocol_t pbus_;
    mmio_buffer_t mmio_;

    void ShutDown();
};
} // namespace gpio
