// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <hw/reg.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>
#include <unistd.h>

#include "astro.h"

#define SOC_WIFI_LPO_32k768 S905D2_GPIOX(16)
#define SOC_BT_REG_ON       S905D2_GPIOX(17)

static const pbus_mmio_t bt_uart_mmios[] = {
    {
        .base = S905D2_UART_A_BASE,
        .length = S905D2_UART_A_LENGTH,
    },
};

static const pbus_irq_t bt_uart_irqs[] = {
    {
        .irq = S905D2_UART_A_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static pbus_dev_t bt_uart_dev = {
    .name = "bt-uart",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_AMLOGIC_UART,
    .serial_port_info = {
        .serial_class = SERIAL_CLASS_BLUETOOTH_HCI,
        .serial_vid = PDEV_VID_BROADCOM,
        .serial_pid = PDEV_PID_BCM43458,
    },
    .mmios = bt_uart_mmios,
    .mmio_count = countof(bt_uart_mmios),
    .irqs = bt_uart_irqs,
    .irq_count = countof(bt_uart_irqs),
};

// Enables and configures PWM_E on the SOC_WIFI_LPO_32k768 line for the Wifi/Bluetooth module
static zx_status_t aml_enable_wifi_32K(aml_bus_t* bus) {
    // Configure SOC_WIFI_LPO_32k768 pin for PWM_E
    zx_status_t status = gpio_set_alt_function(&bus->gpio, SOC_WIFI_LPO_32k768, 1);
    if (status != ZX_OK) return status;

    zx_handle_t bti;
    status = iommu_get_bti(&bus->iommu, 0, BTI_BOARD, &bti);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_enable_wifi_32K: iommu_get_bti failed: %d\n", status);
        return status;
    }
    io_buffer_t buffer;
    status = io_buffer_init_physical(&buffer, bti, S905D2_PWM_BASE, 0x1a000, get_root_resource(),
                                     ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_enable_wifi_32K: io_buffer_init_physical failed: %d\n", status);
        zx_handle_close(bti);
        return status;
    }
    uint32_t* regs = io_buffer_virt(&buffer);

    // these magic numbers were gleaned by instrumenting drivers/amlogic/pwm/pwm_meson.c
    // TODO(voydanoff) write a proper PWM driver
    writel(0x016d016e, regs + S905D2_PWM_PWM_E);
    writel(0x016d016d, regs + S905D2_PWM_E2);
    writel(0x0a0a0609, regs + S905D2_PWM_TIME_EF);
    writel(0x02808003, regs + S905D2_PWM_MISC_REG_EF);

    io_buffer_release(&buffer);
    zx_handle_close(bti);

    return ZX_OK;
}

zx_status_t aml_bluetooth_init(aml_bus_t* bus) {
    zx_status_t status;

    // set alternate functions to enable Bluetooth UART
    status = gpio_set_alt_function(&bus->gpio, S905D2_UART_TX_A, S905D2_UART_TX_A_FN);
    if (status != ZX_OK) return status;
    status = gpio_set_alt_function(&bus->gpio, S905D2_UART_RX_A, S905D2_UART_RX_A_FN);
    if (status != ZX_OK) return status;
    status = gpio_set_alt_function(&bus->gpio, S905D2_UART_CTS_A, S905D2_UART_CTS_A_FN);
    if (status != ZX_OK) return status;
    status = gpio_set_alt_function(&bus->gpio, S905D2_UART_RTS_A, S905D2_UART_RTS_A_FN);
    if (status != ZX_OK) return status;

    // Configure the SOC_WIFI_LPO_32k768 PWM, which is needed for the Bluetooth module to work properly
    status = aml_enable_wifi_32K(bus);
     if (status != ZX_OK) {
        return status;
    }

    // set GPIO to reset Bluetooth module
    gpio_config(&bus->gpio, SOC_BT_REG_ON, GPIO_DIR_OUT);
    gpio_write(&bus->gpio, SOC_BT_REG_ON, 0);
    usleep(10 * 1000);
    gpio_write(&bus->gpio, SOC_BT_REG_ON, 1);
    usleep(100 * 1000);

    // Bind UART for Bluetooth HCI
    status = pbus_device_add(&bus->pbus, &bt_uart_dev, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_uart_init: pbus_device_add failed: %d\n", status);
        return status;
    }

    return ZX_OK;
}
