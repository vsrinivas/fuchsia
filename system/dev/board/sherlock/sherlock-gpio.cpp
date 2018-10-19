// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform-bus.h>

#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"

namespace sherlock {

static const pbus_mmio_t gpio_mmios[] = {
    {
        .base = T931_GPIO_BASE,
        .length = T931_GPIO_LENGTH,
    },
    {
        .base = T931_GPIO_A0_BASE,
        .length = T931_GPIO_AO_LENGTH,
    },
    {
        .base = T931_GPIO_INTERRUPT_BASE,
        .length = T931_GPIO_INTERRUPT_LENGTH,
    },
};

static const pbus_irq_t gpio_irqs[] = {
    {
        .irq = T931_GPIO_IRQ_0,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = T931_GPIO_IRQ_1,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = T931_GPIO_IRQ_2,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = T931_GPIO_IRQ_3,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = T931_GPIO_IRQ_4,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = T931_GPIO_IRQ_5,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = T931_GPIO_IRQ_6,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
    {
        .irq = T931_GPIO_IRQ_7,
        .mode = ZX_INTERRUPT_MODE_DEFAULT,
    },
};

static pbus_dev_t gpio_dev = [](){
    pbus_dev_t dev;
    dev.name = "gpio";
    dev.vid = PDEV_VID_AMLOGIC;
    dev.pid = PDEV_PID_AMLOGIC_T931;
    dev.did = PDEV_DID_AMLOGIC_GPIO;
    dev.mmio_list = gpio_mmios;
    dev.mmio_count = countof(gpio_mmios);
    dev.irq_list = gpio_irqs;
    dev.irq_count = countof(gpio_irqs);
    return dev;
}();

zx_status_t Sherlock::GpioInit() {
    zx_status_t status = pbus_.ProtocolDeviceAdd(ZX_PROTOCOL_GPIO_IMPL, &gpio_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: ProtocolDeviceAdd failed %d\n", __func__, status);
        return status;
    }

    status = device_get_protocol(parent(), ZX_PROTOCOL_GPIO_IMPL, &gpio_impl_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: device_get_protocol failed %d\n", __func__, status);
        return status;
    }

    return ZX_OK;
}

} // namespace sherlock
