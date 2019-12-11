// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/serial/c/fidl.h>
#include <lib/mmio/mmio.h>
#include <unistd.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpioimpl.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/serial.h>
#include <hw/reg.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>
#include <soc/aml-s905d2/s905d2-pwm.h>

#include "astro.h"

namespace astro {

#define SOC_WIFI_LPO_32k768 S905D2_GPIOX(16)
#define SOC_BT_REG_ON S905D2_GPIOX(17)

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
    .serial_class = fuchsia_hardware_serial_Class_BLUETOOTH_HCI,
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

static pbus_dev_t bt_uart_dev = []() {
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

// Composite binding rules for bluetooth.
constexpr zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
constexpr zx_bind_inst_t pwm_e_match[] = {
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PWM),
    BI_MATCH_IF(EQ, BIND_PWM_ID, S905D2_PWM_E),
};
constexpr device_component_part_t pwm_e_component[] = {
    {countof(root_match), root_match},
    {countof(pwm_e_match), pwm_e_match},
};
constexpr device_component_t bt_uart_components[] = {
    {countof(pwm_e_component), pwm_e_component},
};

// Enables and configures PWM_E on the SOC_WIFI_LPO_32k768 line for the
// Wifi/Bluetooth module
zx_status_t Astro::EnableWifi32K() {
  // Configure SOC_WIFI_LPO_32k768 pin for PWM_E
  return gpio_impl_.SetAltFunction(SOC_WIFI_LPO_32k768, 1);
}

zx_status_t Astro::BluetoothInit() {
  zx_status_t status;

  // set alternate functions to enable Bluetooth UART
  status = gpio_impl_.SetAltFunction(S905D2_UART_TX_A, S905D2_UART_TX_A_FN);
  if (status != ZX_OK) {
    return status;
  }

  status = gpio_impl_.SetAltFunction(S905D2_UART_RX_A, S905D2_UART_RX_A_FN);
  if (status != ZX_OK) {
    return status;
  }

  status = gpio_impl_.SetAltFunction(S905D2_UART_CTS_A, S905D2_UART_CTS_A_FN);
  if (status != ZX_OK) {
    return status;
  }

  status = gpio_impl_.SetAltFunction(S905D2_UART_RTS_A, S905D2_UART_RTS_A_FN);
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
  status = pbus_.CompositeDeviceAdd(&bt_uart_dev, bt_uart_components, countof(bt_uart_components),
                                    UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed: %d\n", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace astro
