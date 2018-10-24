// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/camera.h>
#include <ddk/platform-defs.h>
#include <ddktl/protocol/gpio-impl.h>
#include <fbl/unique_ptr.h>
#include <hw/reg.h>
#include <soc/aml-meson/g12b-clk.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"

namespace sherlock {

namespace {

constexpr uint32_t kClk24MAltFunc = 7;
constexpr uint32_t kI2cSDAAltFunc = 2;
constexpr uint32_t kI2cSCLAltFunc = 2;

constexpr pbus_mmio_t mipi_mmios[] = {
    // CSI PHY0
    {
        .base = T931_CSI_PHY0_BASE,
        .length = T931_CSI_PHY0_LENGTH,
    },
    // Analog PHY
    {
        .base = T931_APHY_BASE,
        .length = T931_APHY_LENGTH,
    },
    // CSI HOST0
    {
        .base = T931_CSI_HOST0_BASE,
        .length = T931_CSI_HOST0_LENGTH,
    },
    // MIPI Adapter
    {
        .base = T931_MIPI_ADAPTER_BASE,
        .length = T931_MIPI_ADAPTER_LENGTH,
    },
    // HIU for clocks.
    {
        .base = T931_HIU_BASE,
        .length = T931_HIU_LENGTH,
    },
    // Power domain
    {
        .base = T931_POWER_DOMAIN_BASE,
        .length = T931_POWER_DOMAIN_LENGTH,
    },
    // Memory PD
    {
        .base = T931_MEMORY_PD_BASE,
        .length = T931_MEMORY_PD_LENGTH,
    },
    // Reset
    {
        .base = T931_RESET_BASE,
        .length = T931_RESET_LENGTH,
    },
};

constexpr camera_sensor_t mipi_sensor[] = {
    {
        .vid = PDEV_VID_SONY,
        .pid = PDEV_PID_SONY_IMX227,
        .did = PDEV_DID_CAMERA_SENSOR,
    },
};

constexpr pbus_bti_t mipi_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_CAMERA,
    },
};

constexpr pbus_irq_t mipi_irqs[] = {
    {
        .irq = T931_MIPI_ADAPTER_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }};

constexpr pbus_metadata_t mipi_metadata[] = {
    {
        .type = DEVICE_METADATA_PRIVATE,
        .data_buffer = &mipi_sensor,
        .data_size = sizeof(camera_sensor_t),
    },
};

constexpr pbus_i2c_channel_t sensor_i2c[] = {
    {
        .bus_id = SHERLOCK_I2C_3,
        .address = 0x36,
    },
};

constexpr pbus_gpio_t sensor_gpios[] = {
    {
        // vana-enable
        .gpio = T931_GPIOA(6),
    },
    {
        // vdig-enable
        .gpio = T931_GPIOZ(12),
    },
    {
        // camera sensor reset
        .gpio = T931_GPIOZ(0),
    },
};

static const pbus_clk_t sensor_clk_gates[] = {
    {
        .clk = G12B_CLK_CAM_INCK_24M,
    },
};

static const pbus_dev_t mipi_children = []() {
    // Sony IMX 227 Camera Sensor
    pbus_dev_t dev;
    dev.name = "imx227";
    dev.i2c_channel_list = sensor_i2c;
    dev.i2c_channel_count = countof(sensor_i2c);
    dev.gpio_list = sensor_gpios;
    dev.gpio_count = countof(sensor_gpios);
    dev.clk_list = sensor_clk_gates;
    dev.clk_count = countof(sensor_clk_gates);
    return dev;
}();

static pbus_dev_t mipi_dev = []() {
    pbus_dev_t dev;
    dev.name = "mipi-csi2";
    dev.vid = PDEV_VID_AMLOGIC;
    dev.pid = PDEV_PID_AMLOGIC_T931;
    dev.did = PDEV_DID_AMLOGIC_MIPI;
    dev.mmio_list = mipi_mmios;
    dev.mmio_count = countof(mipi_mmios);
    dev.metadata_list = mipi_metadata;
    dev.metadata_count = countof(mipi_metadata);
    dev.child_list = &mipi_children;
    dev.child_count = 1;
    dev.bti_list = mipi_btis;
    dev.bti_count = countof(mipi_btis);
    dev.irq_list = mipi_irqs;
    dev.irq_count = countof(mipi_irqs);
    return dev;
}();

} // namespace

zx_status_t Sherlock::CameraInit() {

    // Set GPIO alternate functions.
    ddk::GpioImplProtocolProxy gpio_impl(&gpio_impl_);
    gpio_impl.SetAltFunction(T931_GPIOAO(10), kClk24MAltFunc);

    gpio_impl.SetAltFunction(T931_GPIOA(14), kI2cSDAAltFunc);
    gpio_impl.SetAltFunction(T931_GPIOA(15), kI2cSCLAltFunc);

    zx_status_t status = pbus_.DeviceAdd(&mipi_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DeviceAdd failed %d\n", __func__, status);
        return status;
    }

    return status;
}

} // namespace sherlock
