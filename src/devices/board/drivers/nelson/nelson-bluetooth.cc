// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.platform.bus/cpp/driver/fidl.h>
#include <fidl/fuchsia.hardware.platform.bus/cpp/fidl.h>
#include <fuchsia/hardware/gpioimpl/c/banjo.h>
#include <fuchsia/hardware/serial/c/banjo.h>
#include <fuchsia/hardware/serial/c/fidl.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/hw/reg.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/mmio/mmio.h>
#include <unistd.h>

#include <ddk/metadata/init-step.h>
#include <soc/aml-s905d3/s905d3-gpio.h>
#include <soc/aml-s905d3/s905d3-hw.h>

#include "nelson.h"
#include "src/devices/board/drivers/nelson/nelson_bt_uart_bind.h"
#include "src/devices/bus/lib/platform-bus-composites/platform-bus-composite.h"

namespace nelson {
namespace fpbus = fuchsia_hardware_platform_bus;

static const std::vector<fpbus::Mmio> bt_uart_mmios{
    {{
        .base = S905D3_UART_A_BASE,
        .length = S905D3_UART_A_LENGTH,
    }},
};

static const std::vector<fpbus::Irq> bt_uart_irqs{
    {{
        .irq = S905D3_UART_A_IRQ,
        .mode = ZX_INTERRUPT_MODE_EDGE_HIGH,
    }},
};

static const serial_port_info_t bt_uart_serial_info = {
    .serial_class = fuchsia_hardware_serial_Class_BLUETOOTH_HCI,
    .serial_vid = PDEV_VID_BROADCOM,
    .serial_pid = PDEV_PID_BCM43458,
};

static const std::vector<fpbus::Metadata> bt_uart_metadata{
    {{
        .type = DEVICE_METADATA_SERIAL_PORT_INFO,
        .data = std::vector<uint8_t>(
            reinterpret_cast<const uint8_t*>(&bt_uart_serial_info),
            reinterpret_cast<const uint8_t*>(&bt_uart_serial_info) + sizeof(bt_uart_serial_info)),
    }},
};

static const std::vector<fpbus::BootMetadata> bt_uart_boot_metadata{
    {{
        .zbi_type = DEVICE_METADATA_MAC_ADDRESS,
        .zbi_extra = MACADDR_BLUETOOTH,
    }},
};

static const fpbus::Node bt_uart_dev = []() {
  fpbus::Node dev = {};
  dev.name() = "bt-uart";
  dev.vid() = PDEV_VID_AMLOGIC;
  dev.pid() = PDEV_PID_GENERIC;
  dev.did() = PDEV_DID_AMLOGIC_UART;
  dev.mmio() = bt_uart_mmios;
  dev.irq() = bt_uart_irqs;
  dev.metadata() = bt_uart_metadata;
  dev.boot_metadata() = bt_uart_boot_metadata;
  return dev;
}();

zx_status_t Nelson::BluetoothInit() {
  zx_status_t status;

  // set alternate functions to enable Bluetooth UART
  status = gpio_impl_.SetAltFunction(S905D3_UART_TX_A, S905D3_UART_TX_A_FN);
  if (status != ZX_OK) {
    return status;
  }

  status = gpio_impl_.SetAltFunction(S905D3_UART_RX_A, S905D3_UART_RX_A_FN);
  if (status != ZX_OK) {
    return status;
  }

  status = gpio_impl_.SetAltFunction(S905D3_UART_CTS_A, S905D3_UART_CTS_A_FN);
  if (status != ZX_OK) {
    return status;
  }

  status = gpio_impl_.SetAltFunction(S905D3_UART_RTS_A, S905D3_UART_RTS_A_FN);
  if (status != ZX_OK) {
    return status;
  }

  // Bind UART for Bluetooth HCI
  fidl::Arena<> fidl_arena;
  fdf::Arena arena('BLUE');
  auto result = pbus_.buffer(arena)->AddComposite(
      fidl::ToWire(fidl_arena, bt_uart_dev),
      platform_bus_composite::MakeFidlFragment(fidl_arena, bt_uart_fragments,
                                               std::size(bt_uart_fragments)),
      "pdev");
  if (!result.ok()) {
    zxlogf(ERROR, "%s: AddComposite Bluetooth(bt_uart_dev) request failed: %s", __func__,
           result.FormatDescription().data());
    return result.status();
  }
  if (result->is_error()) {
    zxlogf(ERROR, "%s: AddComposite Bluetooth(bt_uart_dev) failed: %s", __func__,
           zx_status_get_string(result->error_value()));
    return result->error_value();
  }

  return ZX_OK;
}

}  // namespace nelson
