// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PLATFORM_PC_DEBUG_H_
#define ZIRCON_KERNEL_PLATFORM_PC_DEBUG_H_

#include <stdint.h>
#include <zircon/boot/image.h>
#include <zircon/types.h>

struct DebugUartInfo {
  enum class Type {
    None,
    Port,
    Mmio,
  };

  uint64_t mem_addr;
  uint32_t io_port;
  uint32_t irq;
  Type type;
};

DebugUartInfo debug_uart_info();

// Parse a "kernel.serial" argument value. If the result is ZX_OK, then uart will
// contain a user-specified UART configuration.
zx_status_t parse_serial_cmdline(const char* serial_mode, zbi_uart_t* uart);

#endif  // ZIRCON_KERNEL_PLATFORM_PC_DEBUG_H_
