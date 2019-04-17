// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/mmio-buffer.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpioimpl.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/serial.h>
#include <hw/reg.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>
#include <zircon/device/serial.h>

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

static const serial_port_info_t bt_uart_serial_info = {
    .serial_class = SERIAL_CLASS_BLUETOOTH_HCI,
    .serial_vid = PDEV_VID_BROADCOM,
    .serial_pid = PDEV_PID_BCM43458,
};

static const pbus_metadata_t bt_uart_metadata[] = {
    {
        .type = DEVICE_METADATA_SERIAL_PORT_INFO,
        .data_buffer = &bt_uart_serial_info,
        .data_size = sizeof(bt_uart_serial_info),
    },
};

static const pbus_boot_metadata_t bt_uart_boot_metadata[] = {
    {
        .zbi_type = DEVICE_METADATA_MAC_ADDRESS,
        .zbi_extra = MACADDR_BLUETOOTH,
    },
};

static pbus_dev_t bt_uart_dev = {
    .name = "bt-uart",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_AMLOGIC_UART,
    .mmio_list = bt_uart_mmios,
    .mmio_count = countof(bt_uart_mmios),
    .irq_list = bt_uart_irqs,
    .irq_count = countof(bt_uart_irqs),
    .metadata_list = bt_uart_metadata,
    .metadata_count = countof(bt_uart_metadata),
    .boot_metadata_list = bt_uart_boot_metadata,
    .boot_metadata_count = countof(bt_uart_boot_metadata),
};

// Enables and configures PWM_E on the SOC_WIFI_LPO_32k768 line for the Wifi/Bluetooth module
static zx_status_t aml_enable_wifi_32K(aml_bus_t* bus) {
    // Configure SOC_WIFI_LPO_32k768 pin for PWM_E
    zx_status_t status = gpio_impl_set_alt_function(&bus->gpio, SOC_WIFI_LPO_32k768, 1);
    if (status != ZX_OK) return status;

    zx_handle_t bti;
    status = iommu_get_bti(&bus->iommu, 0, BTI_BOARD, &bti);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_enable_wifi_32K: iommu_get_bti failed: %d\n", status);
        return status;
    }
    mmio_buffer_t buffer;
    // Please do not use get_root_resource() in new code. See ZX-1497.
    status = mmio_buffer_init_physical(&buffer, S905D2_PWM_BASE, 0x1a000, get_root_resource(),
                                       ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_enable_wifi_32K: mmio_buffer_init_physical failed: %d\n", status);
        zx_handle_close(bti);
        return status;
    }
    uint32_t* regs = buffer.vaddr;

    // these magic numbers were gleaned by instrumenting drivers/amlogic/pwm/pwm_meson.c
    // TODO(voydanoff) write a proper PWM driver
    writel(0x016d016e, regs + S905D2_PWM_PWM_E);
    writel(0x016d016d, regs + S905D2_PWM_E2);
    writel(0x0a0a0609, regs + S905D2_PWM_TIME_EF);
    writel(0x02808003, regs + S905D2_PWM_MISC_REG_EF);

    mmio_buffer_release(&buffer);
    zx_handle_close(bti);

    return ZX_OK;
}

zx_status_t aml_bluetooth_init(aml_bus_t* bus) {
    zx_status_t status;

    // set alternate functions to enable Bluetooth UART
    status = gpio_impl_set_alt_function(&bus->gpio, S905D2_UART_TX_A, S905D2_UART_TX_A_FN);
    if (status != ZX_OK) return status;
    status = gpio_impl_set_alt_function(&bus->gpio, S905D2_UART_RX_A, S905D2_UART_RX_A_FN);
    if (status != ZX_OK) return status;
    status = gpio_impl_set_alt_function(&bus->gpio, S905D2_UART_CTS_A, S905D2_UART_CTS_A_FN);
    if (status != ZX_OK) return status;
    status = gpio_impl_set_alt_function(&bus->gpio, S905D2_UART_RTS_A, S905D2_UART_RTS_A_FN);
    if (status != ZX_OK) return status;

    // Configure the SOC_WIFI_LPO_32k768 PWM, which is needed for the Bluetooth module to work properly
    status = aml_enable_wifi_32K(bus);
     if (status != ZX_OK) {
        return status;
    }

    // set GPIO to reset Bluetooth module
    gpio_impl_config_out(&bus->gpio, SOC_BT_REG_ON, 0);
    usleep(10 * 1000);
    gpio_impl_write(&bus->gpio, SOC_BT_REG_ON, 1);
    usleep(100 * 1000);

    // Bind UART for Bluetooth HCI
    status = pbus_device_add(&bus->pbus, &bt_uart_dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_uart_init: pbus_device_add failed: %d\n", status);
        return status;
    }

    return ZX_OK;
}
