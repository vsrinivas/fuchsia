// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/platform-defs.h>
#include <ddk/protocol/platform-device-lib.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/gpioimpl.h>
#include <hwreg/bitfields.h>
#include <zircon/types.h>

#include "msm8x53-gpio-regs.h"

namespace gpio {

class Msm8x53GpioDevice;
using DeviceType = ddk::Device<Msm8x53GpioDevice, ddk::Unbindable>;

class Msm8x53GpioDevice : public DeviceType,
                          public ddk::GpioImplProtocol<Msm8x53GpioDevice, ddk::base_protocol> {
public:
    static zx_status_t Create(zx_device_t* parent);

    explicit Msm8x53GpioDevice(zx_device_t* parent, mmio_buffer_t gpio_mmio)
        : DeviceType(parent),
          gpio_mmio_(gpio_mmio),
          in_(gpio_mmio),
          out_(gpio_mmio) {}

    zx_status_t Bind();
    zx_status_t Init();

    // Methods required by the ddk mixins
    void DdkUnbind();
    void DdkRelease();

    zx_status_t GpioImplConfigIn(uint32_t index, uint32_t flags);
    zx_status_t GpioImplConfigOut(uint32_t index, uint8_t initial_value);
    zx_status_t GpioImplSetAltFunction(uint32_t index, uint64_t function);
    zx_status_t GpioImplRead(uint32_t index, uint8_t* out_value);
    zx_status_t GpioImplWrite(uint32_t index, uint8_t value);
    zx_status_t GpioImplGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq);
    zx_status_t GpioImplReleaseInterrupt(uint32_t index);
    zx_status_t GpioImplSetPolarity(uint32_t index, uint32_t polarity);

private:
    void ShutDown();

    ddk::MmioBuffer gpio_mmio_;
    const GpioInReg in_;
    const GpioOutReg out_;
};
} // namespace gpio
