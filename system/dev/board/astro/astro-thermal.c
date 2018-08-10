// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "astro.h"

static const pbus_mmio_t thermal_mmios[] = {
    {
        .base = S905D2_TEMP_SENSOR_BASE,
        .length = S905D2_TEMP_SENSOR_LENGTH,
    },
    {
        .base = S905D2_GPIO_A0_BASE,
        .length = S905D2_GPIO_AO_LENGTH,
    },
    {
        .base = S905D2_HIU_BASE,
        .length = S905D2_HIU_LENGTH,
    },
    {
        .base = S905D2_AO_PWM_CD_BASE,
        .length = S905D2_AO_PWM_LENGTH,
    }};

static const pbus_irq_t thermal_irqs[] = {
    {
        .irq = S905D2_TS_PLL_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static pbus_dev_t thermal_dev = {
    .name = "aml-thermal",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_AMLOGIC_S905D2,
    .did = PDEV_DID_AMLOGIC_THERMAL,
    .mmios = thermal_mmios,
    .mmio_count = countof(thermal_mmios),
    .irqs = thermal_irqs,
    .irq_count = countof(thermal_irqs),
};

zx_status_t aml_thermal_init(aml_bus_t* bus) {
    // Configure the GPIO to be Output & set it to alternate
    // function 3 which puts in PWM_D mode
    zx_status_t status = gpio_config(&bus->gpio, S905D2_PWM_D, GPIO_DIR_OUT);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_thermal_init: gpio_config failed: %d\n", status);
        return status;
    }

    status = gpio_set_alt_function(&bus->gpio, S905D2_PWM_D, S905D2_PWM_D_FN);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_thermal_init: gpio_set_alt_function failed: %d\n", status);
        return status;
    }

    status = pbus_device_add(&bus->pbus, &thermal_dev, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_thermal_init: pbus_device_add failed: %d\n", status);
        return status;
    }
    return ZX_OK;
}
