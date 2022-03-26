// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_UART_H_
#define ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_UART_H_

#include <lib/uart/all.h>

using UartDriver = uart::all::KernelDriver<uart::BasicIoProvider, uart::Unsynchronized>;

UartDriver& GetUartDriver();

// Wires up the associated UART to stdout via PhysConsole::set_serial.
void SetUartConsole(const uart::all::Driver& uart);

#endif  // ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_UART_H_
