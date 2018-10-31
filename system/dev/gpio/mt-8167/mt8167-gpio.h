// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/platform-defs.h>
#include <ddk/protocol/gpio-impl.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-device-lib.h>
#include <ddk/protocol/platform-device.h>

#include <ddktl/device.h>
#include <ddktl/protocol/gpio-impl.h>

#include <fbl/array.h>

#include <hw/reg.h>
#include <hwreg/mmio.h>

#include <lib/zx/interrupt.h>
#include <lib/zx/port.h>

namespace gpio {

class Mt8167GpioDevice;
using DeviceType = ddk::Device<Mt8167GpioDevice, ddk::Unbindable>;

class Mt8167GpioDevice : public DeviceType,
                         public ddk::GpioImplProtocol<Mt8167GpioDevice> {
public:
    static zx_status_t Create(zx_device_t* parent);

    explicit Mt8167GpioDevice(zx_device_t* parent, mmio_buffer_t gpio_mmio,
                              mmio_buffer_t iocfg_mmio, mmio_buffer_t eint_mmio)
        : DeviceType(parent),
          gpio_mmio_(gpio_mmio),
          dir_(gpio_mmio),
          out_(gpio_mmio),
          in_(gpio_mmio),
          pull_en_(gpio_mmio),
          pull_sel_(gpio_mmio),
          iocfg_(iocfg_mmio),
          eint_(eint_mmio) {}

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
    zx_status_t GpioImplGetInterrupt(uint32_t index, uint32_t flags, zx_handle_t* out_handle);
    zx_status_t GpioImplReleaseInterrupt(uint32_t index);
    zx_status_t GpioImplSetPolarity(uint32_t index, uint32_t polarity);

private:
    void ShutDown();
    int Thread();

    ddk::MmioBuffer gpio_mmio_;
    const GpioDirReg dir_;
    const GpioOutReg out_;
    const GpioInReg in_;
    const GpioPullEnReg pull_en_;
    const GpioPullSelReg pull_sel_;
    const IoConfigReg iocfg_;
    const ExtendedInterruptReg eint_;
    zx::interrupt int_;
    zx::port port_;
    thrd_t thread_;
    fbl::Array<zx::interrupt> interrupts_;
};
} // namespace gpio
