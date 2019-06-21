// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/serial.h>
#include <ddktl/protocol/gpioimpl.h>
#include <fuchsia/hardware/serial/c/fidl.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>
#include <unistd.h>

#include "sherlock.h"

#define BT_REG_ON T931_GPIOX(17)

namespace sherlock {

namespace {

constexpr pbus_mmio_t bt_uart_mmios[] = {
    {
        .base = T931_UART_A_BASE,
        .length = T931_UART_LENGTH,
    },
};

constexpr pbus_irq_t bt_uart_irqs[] = {
    {
        .irq = T931_UART_A_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    },
};

constexpr serial_port_info_t bt_uart_port_info = {
    .serial_class = fuchsia_hardware_serial_Class_BLUETOOTH_HCI,
    .serial_vid = PDEV_VID_BROADCOM,
    .serial_pid = PDEV_PID_BCM43458,
};

constexpr pbus_metadata_t bt_uart_metadata[] = {
    {
        .type = DEVICE_METADATA_SERIAL_PORT_INFO,
        .data_buffer = &bt_uart_port_info,
        .data_size = sizeof(bt_uart_port_info),
    },
};

constexpr pbus_boot_metadata_t bt_uart_boot_metadata[] = {
    {
        .zbi_type = DEVICE_METADATA_MAC_ADDRESS,
        .zbi_extra = MACADDR_BLUETOOTH,
    },
};

const pbus_dev_t bt_uart_dev = []() {
    pbus_dev_t dev;
    dev.name = "bt-uart";
    dev.vid = PDEV_VID_AMLOGIC;
    dev.pid = PDEV_PID_GENERIC;
    dev.did = PDEV_DID_AMLOGIC_UART;
    dev.mmio_list = bt_uart_mmios;
    dev.mmio_count = countof(bt_uart_mmios);
    dev.irq_list = bt_uart_irqs;
    dev.irq_count = countof(bt_uart_irqs);
    dev.metadata_list = bt_uart_metadata;
    dev.metadata_count = countof(bt_uart_metadata);
    dev.boot_metadata_list = bt_uart_boot_metadata;
    dev.boot_metadata_count = countof(bt_uart_boot_metadata);
    return dev;
}();

} // namespace

zx_status_t Sherlock::BluetoothInit() {
    zx_status_t status;

    if (((status = gpio_impl_.SetAltFunction(T931_UART_A_TX, T931_UART_A_TX_FN)) != ZX_OK) ||
        ((status = gpio_impl_.SetAltFunction(T931_UART_A_RX, T931_UART_A_RX_FN)) != ZX_OK) ||
        ((status = gpio_impl_.SetAltFunction(T931_UART_A_CTS, T931_UART_A_CTS_FN)) != ZX_OK) ||
        ((status = gpio_impl_.SetAltFunction(T931_UART_A_RTS, T931_UART_A_RTS_FN)) != ZX_OK)) {
        return status;
    }

    if ((status = gpio_impl_.ConfigOut(BT_REG_ON, 0) != ZX_OK)) return status;
    usleep(10 * 1000);
    if ((status = gpio_impl_.Write(BT_REG_ON, 1) != ZX_OK)) return status;
    usleep(10 * 1000);

    status = pbus_.DeviceAdd(&bt_uart_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DeviceAdd() error: %d\n", __func__, status);
        return status;
    }

    return ZX_OK;
}

} // namespace sherlock
