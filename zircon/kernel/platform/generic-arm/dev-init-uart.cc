// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/uart/all.h>
#include <lib/uart/null.h>
#include <zircon/boot/driver-config.h>

#include <dev/init.h>
#include <dev/uart/amlogic_s905/init.h>
#include <dev/uart/dw8250/init.h>
#include <dev/uart/motmot/init.h>
#include <dev/uart/pl011/init.h>
#include <ktl/variant.h>

#include <ktl/enforce.h>

namespace {

// Overloads for early UART initialization below.
void UartInitEarly(uint32_t extra, const uart::null::Driver::config_type& config) {}

void UartInitEarly(uint32_t extra, const zbi_dcfg_simple_t& config) {
  switch (extra) {
    case ZBI_KERNEL_DRIVER_AMLOGIC_UART:
      AmlogicS905UartInitEarly(config);
      break;
    case ZBI_KERNEL_DRIVER_DW8250_UART:
      Dw8250UartInitEarly(config);
      break;
    case ZBI_KERNEL_DRIVER_MOTMOT_UART:
      MotmotUartInitEarly(config);
      break;
    case ZBI_KERNEL_DRIVER_PL011_UART:
      Pl011UartInitEarly(config);
      break;
  }
}

void UartInitLate(uint32_t extra) {
  switch (extra) {
    case ZBI_KERNEL_DRIVER_AMLOGIC_UART:
      AmlogicS905UartInitLate();
      break;
    case ZBI_KERNEL_DRIVER_DW8250_UART:
      Dw8250UartInitLate();
      break;
    case ZBI_KERNEL_DRIVER_MOTMOT_UART:
      MotmotUartInitLate();
      break;
    case ZBI_KERNEL_DRIVER_PL011_UART:
      Pl011UartInitLate();
      break;
  }
}

}  // namespace

void PlatformUartDriverHandoffEarly(const uart::all::Driver& serial) {
  ktl::visit([](const auto& uart) { UartInitEarly(uart.extra(), uart.config()); }, serial);
}

void PlatformUartDriverHandoffLate(const uart::all::Driver& serial) {
  ktl::visit([](const auto& uart) { UartInitLate(uart.extra()); }, serial);
}
