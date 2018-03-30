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
#include <soc/aml-s912/s912-gpio.h>
#include <soc/aml-s912/s912-hw.h>
#include <unistd.h>

#include "vim.h"

// set this to enable UART test driver, which uses the second UART
// on the 40 pin header
#define UART_TEST 1

#define WIFI_32K    S912_GPIOX(16)
#define BT_EN       S912_GPIOX(17)


static const pbus_mmio_t bt_uart_mmios[] = {
    // UART_A, for BT HCI
    {
        .base = S912_UART_A_BASE,
        .length = S912_UART_A_LENGTH,
    },
};

static const pbus_irq_t bt_uart_irqs[] = {
    // UART_A, for BT HCI
    {
        .irq = S912_UART_A_IRQ,
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
        .serial_pid = PDEV_PID_BCM4356,
    },
    .mmios = bt_uart_mmios,
    .mmio_count = countof(bt_uart_mmios),
    .irqs = bt_uart_irqs,
    .irq_count = countof(bt_uart_irqs),
};

#if UART_TEST
static const pbus_mmio_t header_uart_mmios[] = {
    // UART_AO_B, on 40 pin header
    {
        .base = S912_UART_AO_B_BASE,
        .length = S912_UART_AO_B_LENGTH,
    },
};

static const pbus_irq_t header_uart_irqs[] = {
    // UART_AO_B, on 40 pin header
    {
        .irq = S912_UART_AO_B_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

static pbus_dev_t header_uart_dev = {
    .name = "header-uart",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_AMLOGIC_UART,
    .serial_port_info = {
        .serial_class = SERIAL_CLASS_GENERIC,
    },
    .mmios = header_uart_mmios,
    .mmio_count = countof(header_uart_mmios),
    .irqs = header_uart_irqs,
    .irq_count = countof(header_uart_irqs),
};
#endif

// Enables and configures PWM_E on the WIFI_32K line for the Wifi/Bluetooth module
static zx_status_t vim_enable_wifi_32K(vim_bus_t* bus) {
    // Configure WIFI_32K pin for PWM_E
    zx_status_t status = gpio_set_alt_function(&bus->gpio, WIFI_32K, 1);
    if (status != ZX_OK) return status;

    zx_handle_t bti;
    status = iommu_get_bti(&bus->iommu, 0, BTI_BOARD, &bti);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_bus_bind: iommu_get_bti failed: %d\n", status);
        return status;
    }
    io_buffer_t buffer;
    status = io_buffer_init_physical(&buffer, bti, S912_PWM_BASE, 0x10000, get_root_resource(),
                                     ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_enable_wifi_32K: io_buffer_init_physical failed: %d\n", status);
        zx_handle_close(bti);
        return status;
    }
    uint32_t* regs = io_buffer_virt(&buffer);

    // these magic numbers were gleaned by instrumenting drivers/amlogic/pwm/pwm_meson.c
    // TODO(voydanoff) write a proper PWM driver
    writel(0x016d016e, regs + S912_PWM_PWM_E);
    writel(0x016d016d, regs + S912_PWM_E2);
    writel(0x0a0a0609, regs + S912_PWM_TIME_EF);
    writel(0x02808003, regs + S912_PWM_MISC_REG_EF);

    io_buffer_release(&buffer);
    zx_handle_close(bti);

    return ZX_OK;
}

zx_status_t vim_uart_init(vim_bus_t* bus) {
    zx_status_t status;

    // set alternate functions to enable UART_A and UART_AO_B
    status = gpio_set_alt_function(&bus->gpio, S912_UART_TX_A, S912_UART_TX_A_FN);
    if (status != ZX_OK) return status;
    status = gpio_set_alt_function(&bus->gpio, S912_UART_RX_A, S912_UART_RX_A_FN);
    if (status != ZX_OK) return status;
    status = gpio_set_alt_function(&bus->gpio, S912_UART_CTS_A, S912_UART_CTS_A_FN);
    if (status != ZX_OK) return status;
    status = gpio_set_alt_function(&bus->gpio, S912_UART_RTS_A, S912_UART_RTS_A_FN);
    if (status != ZX_OK) return status;
    status = gpio_set_alt_function(&bus->gpio, S912_UART_TX_AO_B, S912_UART_TX_AO_B_FN);
    if (status != ZX_OK) return status;
    status = gpio_set_alt_function(&bus->gpio, S912_UART_RX_AO_B, S912_UART_RX_AO_B_FN);
    if (status != ZX_OK) return status;

    // Configure the WIFI_32K PWM, which is needed for the Bluetooth module to work properly
    status = vim_enable_wifi_32K(bus);
     if (status != ZX_OK) {
        return status;
    }

    // set GPIO to reset Bluetooth module
    gpio_config(&bus->gpio, BT_EN, GPIO_DIR_OUT);
    gpio_write(&bus->gpio, BT_EN, 0);
    usleep(10 * 1000);
    gpio_write(&bus->gpio, BT_EN, 1);

    // Bind UART for Bluetooth HCI
    status = pbus_device_add(&bus->pbus, &bt_uart_dev, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_gpio_init: pbus_device_add failed: %d\n", status);
        return status;
    }

#if UART_TEST
    // Bind UART for 40-pin header
    status = pbus_device_add(&bus->pbus, &header_uart_dev, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_gpio_init: pbus_device_add failed: %d\n", status);
        return status;
    }
#endif

    return ZX_OK;
}
