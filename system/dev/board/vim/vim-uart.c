// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <soc/aml-s912/s912-gpio.h>
#include <soc/aml-s912/s912-hw.h>

#include <hw/reg.h>

#include "vim.h"

// set this to enable UART test driver, which uses the second UART
// on the 40 pin header
#define UART_TEST 1

#define BT_EN S912_GPIOX(17)

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

zx_status_t vim_uart_init(vim_bus_t* bus) {
    // set alternate functions to enable UART_A and UART_AO_B
    gpio_set_alt_function(&bus->gpio, S912_UART_TX_A, S912_UART_TX_A_FN);
    gpio_set_alt_function(&bus->gpio, S912_UART_RX_A, S912_UART_RX_A_FN);
    gpio_set_alt_function(&bus->gpio, S912_UART_CTS_A, S912_UART_CTS_A_FN);
    gpio_set_alt_function(&bus->gpio, S912_UART_RTS_A, S912_UART_RTS_A_FN);
    gpio_set_alt_function(&bus->gpio, S912_UART_TX_AO_B, S912_UART_TX_AO_B_FN);
    gpio_set_alt_function(&bus->gpio, S912_UART_RX_AO_B, S912_UART_RX_AO_B_FN);

    // bind our UART driver
    zx_status_t status = pbus_device_add(&bus->pbus, &uart_dev, PDEV_ADD_PBUS_DEVHOST);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_gpio_init: pbus_device_add failed: %d\n", status);
        return status;
    }

    status = pbus_wait_protocol(&bus->pbus, ZX_PROTOCOL_SERIAL_DRIVER);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_gpio_init: pbus_wait_protocol failed: %d\n", status);
        return status;
    }

    status = device_get_protocol(bus->parent, ZX_PROTOCOL_SERIAL_DRIVER, &bus->serial);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_gpio_init: device_get_protocol failed: %d\n", status);
        return status;
    }

    // set GPIO to enable Bluetooth module
    gpio_config(&bus->gpio, BT_EN, GPIO_DIR_OUT);
    gpio_write(&bus->gpio, BT_EN, 1);

    serial_driver_config(&bus->serial, 0, 115200, SERIAL_DATA_BITS_8 | SERIAL_STOP_BITS_1 |
                                                  SERIAL_PARITY_NONE | SERIAL_FLOW_CTRL_CTS_RTS);

    // bind Bluetooth HCI UART driver
    status = pbus_device_add(&bus->pbus, &bt_uart_dev, 0);
    if (status != ZX_OK) {
        zxlogf(ERROR, "vim_gpio_init: pbus_device_add failed: %d\n", status);
        return status;
    }

#if UART_TEST
    serial_driver_config(&bus->serial, 1, 115200, SERIAL_DATA_BITS_8 | SERIAL_STOP_BITS_1 |
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
