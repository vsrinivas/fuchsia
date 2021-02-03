// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/gpioimpl/c/banjo.h>
#include <fuchsia/hardware/platform/bus/c/banjo.h>
#include <fuchsia/hardware/serial/c/banjo.h>
#include <fuchsia/hardware/serial/c/fidl.h>
#include <lib/mmio/mmio.h>
#include <unistd.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/metadata.h>
#include <ddk/metadata/init-step.h>
#include <ddk/platform-defs.h>
#include <hw/reg.h>
#include <soc/aml-t931/t931-gpio.h>
#include <soc/aml-t931/t931-hw.h>

#include "sherlock.h"

namespace sherlock {

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

const pbus_metadata_t bt_uart_metadata[] = {
    {
        .type = DEVICE_METADATA_SERIAL_PORT_INFO,
        .data_buffer = reinterpret_cast<const uint8_t*>(&bt_uart_serial_info),
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

// Composite binding rules for bluetooth.
constexpr zx_bind_inst_t root_match[] = {
    BI_MATCH(),
};
constexpr zx_bind_inst_t pwm_e_match[] = {
    BI_MATCH_IF(EQ, BIND_INIT_STEP, BIND_INIT_STEP_PWM),
};
constexpr device_fragment_part_t pwm_e_fragment[] = {
    {countof(root_match), root_match},
    {countof(pwm_e_match), pwm_e_match},
};
constexpr device_fragment_t bt_uart_fragments[] = {
    {"pwm", countof(pwm_e_fragment), pwm_e_fragment},
};

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

  // Bind UART for Bluetooth HCI
  status = pbus_.CompositeDeviceAdd(&bt_uart_dev, reinterpret_cast<uint64_t>(bt_uart_fragments),
                                    countof(bt_uart_fragments), UINT32_MAX);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DeviceAdd failed: %d", __func__, status);
    return status;
  }

  return ZX_OK;
}

}  // namespace sherlock
