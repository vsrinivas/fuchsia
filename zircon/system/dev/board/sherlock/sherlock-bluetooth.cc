// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/serial/c/fidl.h>
#include <lib/mmio/mmio.h>
#include <unistd.h>

#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpioimpl.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/serial.h>
#include <hw/reg.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"

namespace sherlock {

#define SOC_WIFI_LPO_32k768 T931_GPIOX(16)
#define SOC_BT_REG_ON T931_GPIOX(17)

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

constexpr serial_port_info_t bt_uart_serial_info = {
    .serial_class = fuchsia_hardware_serial_Class_BLUETOOTH_HCI,
    .serial_vid = PDEV_VID_BROADCOM,
    .serial_pid = PDEV_PID_BCM43458,
};

constexpr pbus_metadata_t bt_uart_metadata[] = {
    {
        .type = DEVICE_METADATA_SERIAL_PORT_INFO,
        .data_buffer = &bt_uart_serial_info,
        .data_size = sizeof(bt_uart_serial_info),
    },
};

constexpr pbus_boot_metadata_t bt_uart_boot_metadata[] = {
    {
        .zbi_type = DEVICE_METADATA_MAC_ADDRESS,
        .zbi_extra = MACADDR_BLUETOOTH,
    },
};

static const pbus_dev_t bt_uart_dev = []() {
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
  dev.boot_metadata_list = bt_uart_boot_metadata;
  dev.boot_metadata_count = countof(bt_uart_boot_metadata);
  return dev;
}();

// Enables and configures PWM_E on the SOC_WIFI_LPO_32k768 line for the
// Wifi/Bluetooth module
zx_status_t Sherlock::EnableWifi32K() {
  // Configure SOC_WIFI_LPO_32k768 pin for PWM_E
  zx_status_t status = gpio_impl_.SetAltFunction(SOC_WIFI_LPO_32k768, 1);
  if (status != ZX_OK) {
    return status;
  }

  zx::bti bti;
  status = iommu_.GetBti(BTI_BOARD, 0, &bti);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: GetBti failed: %d\n", __func__, status);
    return status;
  }

  return ZX_OK;
}

zx_status_t Sherlock::BluetoothInit() {
  zx_status_t status;

  // set alternate functions to enable Bluetooth UART
  status = gpio_impl_.SetAltFunction(T931_UART_A_TX, T931_UART_A_TX_FN);
  if (status != ZX_OK) {
    return status;
  }

  status = gpio_impl_.SetAltFunction(T931_UART_A_RX, T931_UART_A_RX_FN);
  if (status != ZX_OK) {
    return status;
  }

  status = gpio_impl_.SetAltFunction(T931_UART_A_CTS, T931_UART_A_CTS_FN);
  if (status != ZX_OK) {
    return status;
  }

  status = gpio_impl_.SetAltFunction(T931_UART_A_RTS, T931_UART_A_RTS_FN);
  if (status != ZX_OK) {
    return status;
  }

  // Configure the SOC_WIFI_LPO_32k768 PWM, which is needed for the
  // Bluetooth module to work properly
  status = EnableWifi32K();
  if (status != ZX_OK) {
    return status;
  }

  // set GPIO to reset Bluetooth module
  gpio_impl_.ConfigOut(SOC_BT_REG_ON, 0);
  usleep(10 * 1000);
  gpio_impl_.Write(SOC_BT_REG_ON, 1);
  usleep(100 * 1000);

  // Bind UART for Bluetooth HCI
  status = pbus_.DeviceAdd(&bt_uart_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed: %d\n", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace sherlock
