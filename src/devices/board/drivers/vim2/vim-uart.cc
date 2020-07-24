// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/serial/c/fidl.h>
#include <unistd.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/mmio-buffer.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/serial.h>
#include <hw/reg.h>
#include <soc/aml-s912/s912-gpio.h>
#include <soc/aml-s912/s912-hw.h>

#include "vim.h"

namespace vim {
// set this to enable UART test driver, which uses the second UART
// on the 40 pin header
#define UART_TEST 1

#define WIFI_32K S912_GPIOX(16)
#define BT_EN S912_GPIOX(17)

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

static const serial_port_info_t bt_uart_serial_info = {
    .serial_class = fuchsia_hardware_serial_Class_BLUETOOTH_HCI,
    .serial_vid = PDEV_VID_BROADCOM,
    .serial_pid = PDEV_PID_BCM4356,
};

static const pbus_metadata_t bt_uart_metadata[] = {
    {
        .type = DEVICE_METADATA_SERIAL_PORT_INFO,
        .data_buffer = &bt_uart_serial_info,
        .data_size = sizeof(bt_uart_serial_info),
    },
};

static pbus_dev_t bt_uart_dev = [] {
  pbus_dev_t dev = {};
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
  return dev;
}();

// Composite binding rules for bluetooth.
constexpr device_fragment_t bt_uart_fragments[] = {};

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

static const serial_port_info_t header_serial_info = []() {
  serial_port_info_t serial_port_info;
  serial_port_info.serial_class = fuchsia_hardware_serial_Class_GENERIC;
  return serial_port_info;
}();

static const pbus_metadata_t header_metadata[] = {
    {
        .type = DEVICE_METADATA_SERIAL_PORT_INFO,
        .data_buffer = &header_serial_info,
        .data_size = sizeof(header_serial_info),
    },
};

static pbus_dev_t header_uart_dev = []() {
  pbus_dev_t dev = {};
  dev.name = "header-uart";
  dev.vid = PDEV_VID_AMLOGIC;
  dev.pid = PDEV_PID_GENERIC;
  dev.did = PDEV_DID_AMLOGIC_UART;
  dev.mmio_list = header_uart_mmios;
  dev.mmio_count = countof(header_uart_mmios);
  dev.irq_list = header_uart_irqs;
  dev.irq_count = countof(header_uart_irqs);
  dev.metadata_list = header_metadata;
  dev.metadata_count = countof(header_metadata);
  return dev;
}();
#endif

// Enables and configures PWM_E on the WIFI_32K line for the Wifi/Bluetooth module
zx_status_t Vim::EnableWifi32K() {
  // Configure WIFI_32K pin for PWM_E
  zx_status_t status = gpio_impl_.SetAltFunction(WIFI_32K, 1);
  if (status != ZX_OK)
    return status;

  mmio_buffer_t buffer;
  // Please do not use get_root_resource() in new code. See ZX-1467.
  status = mmio_buffer_init_physical(&buffer, S912_PWM_BASE, 0x10000, get_root_resource(),
                                     ZX_CACHE_POLICY_UNCACHED_DEVICE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "vim_enable_wifi_32K: io_buffer_init_physical failed: %d", status);
    return status;
  }
  MMIO_PTR uint32_t* regs = (MMIO_PTR uint32_t*)buffer.vaddr;

  // these magic numbers were gleaned by instrumenting drivers/amlogic/pwm/pwm_meson.c
  // TODO(voydanoff) write a proper PWM driver
  MmioWrite32(0x016d016e, regs + S912_PWM_PWM_E);
  MmioWrite32(0x016d016d, regs + S912_PWM_E2);
  MmioWrite32(0x0a0a0609, regs + S912_PWM_TIME_EF);
  MmioWrite32(0x02808003, regs + S912_PWM_MISC_REG_EF);

  mmio_buffer_release(&buffer);

  return ZX_OK;
}

zx_status_t Vim::UartInit() {
  zx_status_t status;

  // set alternate functions to enable UART_A and UART_AO_B
  status = gpio_impl_.SetAltFunction(S912_UART_TX_A, S912_UART_TX_A_FN);
  if (status != ZX_OK)
    return status;
  status = gpio_impl_.SetAltFunction(S912_UART_RX_A, S912_UART_RX_A_FN);
  if (status != ZX_OK)
    return status;
  status = gpio_impl_.SetAltFunction(S912_UART_CTS_A, S912_UART_CTS_A_FN);
  if (status != ZX_OK)
    return status;
  status = gpio_impl_.SetAltFunction(S912_UART_RTS_A, S912_UART_RTS_A_FN);
  if (status != ZX_OK)
    return status;
  status = gpio_impl_.SetAltFunction(S912_UART_TX_AO_B, S912_UART_TX_AO_B_FN);
  if (status != ZX_OK)
    return status;
  status = gpio_impl_.SetAltFunction(S912_UART_RX_AO_B, S912_UART_RX_AO_B_FN);
  if (status != ZX_OK)
    return status;

  // Configure the WIFI_32K PWM, which is needed for the Bluetooth module to work properly
  status = EnableWifi32K();
  if (status != ZX_OK) {
    return status;
  }

  // set GPIO to reset Bluetooth module
  gpio_impl_.ConfigOut(BT_EN, 0);
  usleep(10 * 1000);
  gpio_impl_.Write(BT_EN, 1);

  // Bind UART for Bluetooth HCI
  status = pbus_.CompositeDeviceAdd(&bt_uart_dev, bt_uart_fragments, countof(bt_uart_fragments),
                                    UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "UartInit: pbus_device_add failed: %d", status);
    return status;
  }

#if UART_TEST
  // Bind UART for 40-pin header
  status = pbus_.DeviceAdd(&header_uart_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "UartInit: pbus_device_add failed: %d", status);
    return status;
  }
#endif

  return ZX_OK;
}
}  // namespace vim
