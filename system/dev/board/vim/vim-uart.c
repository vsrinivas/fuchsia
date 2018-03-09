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

static const pbus_mmio_t uart_mmios[] = {
    // UART_A, for BT HCI
    {
        .base = 0xc11084c0,
        .length = 0x18,
    },
#if UART_TEST
    // UART_AO_B, on 40 pin header
    {
        .base = 0xc81004e0,
        .length = 0x18,
    },
#endif
};

static const pbus_irq_t uart_irqs[] = {
    // UART_A, for BT HCI
    {
        .irq = 58,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
#if UART_TEST
    // UART_AO_B, on 40 pin header
    {
        .irq = 229,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
#endif
};

static pbus_dev_t uart_dev = {
    .name = "uart",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_AMLOGIC_UART,
    .mmios = uart_mmios,
    .mmio_count = countof(uart_mmios),
    .irqs = uart_irqs,
    .irq_count = countof(uart_irqs),
};

const pbus_uart_t bt_uarts[] = {
    {
        .port = 0,
    },
};


static const pbus_dev_t bt_uart_dev = {
    .name = "bt-uart-hci",
    .vid = PDEV_VID_BROADCOM,
    .pid = PDEV_PID_BCM4356,
    .did = PDEV_DID_BT_UART,
    .uarts = bt_uarts,
    .uart_count = countof(bt_uarts),
};

#if UART_TEST
const pbus_uart_t uart_test_uarts[] = {
    {
        .port = 1,
    },
};

static pbus_dev_t uart_test_dev = {
    .name = "uart-test",
    .vid = PDEV_VID_GENERIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_UART_TEST,
    .uarts = uart_test_uarts,
    .uart_count = countof(uart_test_uarts),
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
    status = io_buffer_init_physical_with_bti(&buffer, bti, S912_PWM_BASE, 0x10000,
                                              get_root_resource(), ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_enable_wifi_32K: io_buffer_init_physical_with_bti failed: %d\n", status);
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

    // bind our UART driver
    status = pbus_device_add(&bus->pbus, &uart_dev, PDEV_ADD_PBUS_DEVHOST);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_gpio_init: pbus_device_add failed: %d\n", status);
        return status;
    }

    status = pbus_wait_protocol(&bus->pbus, ZX_PROTOCOL_SERIAL_IMPL);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_gpio_init: pbus_wait_protocol failed: %d\n", status);
        return status;
    }

    status = device_get_protocol(bus->parent, ZX_PROTOCOL_SERIAL_IMPL, &bus->serial);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_gpio_init: device_get_protocol failed: %d\n", status);
        return status;
    }

    // set GPIO to reset Bluetooth module
    gpio_config(&bus->gpio, BT_EN, GPIO_DIR_OUT);
    gpio_write(&bus->gpio, BT_EN, 0);
    usleep(10 * 1000);
    gpio_write(&bus->gpio, BT_EN, 1);

    serial_impl_config(&bus->serial, 0, 115200, SERIAL_DATA_BITS_8 | SERIAL_STOP_BITS_1 |
                                                SERIAL_PARITY_NONE | SERIAL_FLOW_CTRL_CTS_RTS);

    // bind Bluetooth HCI UART driver
    status = pbus_device_add(&bus->pbus, &bt_uart_dev, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_gpio_init: pbus_device_add failed: %d\n", status);
        return status;
    }

#if UART_TEST
    serial_impl_config(&bus->serial, 1, 115200, SERIAL_DATA_BITS_8 | SERIAL_STOP_BITS_1 |
                                                SERIAL_PARITY_NONE);
    // Bind UART test driver
    status = pbus_device_add(&bus->pbus, &uart_test_dev, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_gpio_init: pbus_device_add failed: %d\n", status);
        return status;
    }
#endif

    return ZX_OK;
}
