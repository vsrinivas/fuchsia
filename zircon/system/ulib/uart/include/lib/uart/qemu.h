// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UART_QEMU_H_
#define LIB_UART_QEMU_H_

// QEMU-only tests and boot shims hard-code a particular driver configuration.

#include "ns8250.h"
#include "null.h"
#include "pl011.h"
#include "uart.h"

namespace uart {
namespace qemu {

// uart::qemu::Driver is a default-constructible UartDriver type.

#ifdef __aarch64__

struct Driver : public pl011::Driver {
  Driver(dcfg_simple_t cfg = pl011::kQemuConfig) : pl011::Driver(cfg) {}
};

#elif defined(__x86_64__) || defined(__i386__)

struct Driver : public ns8250::PioDriver {
  explicit Driver(dcfg_simple_pio_t cfg = ns8250::kLegacyConfig) : ns8250::PioDriver(cfg) {}
};

#else

using Driver = null::Driver;

#endif

// uart::qemu::KernelDriver is default-constructible and usable right away.

template <template <typename> class IoProvider = BasicIoProvider, typename Sync = Unsynchronized>
using KernelDriver = uart::KernelDriver<Driver, IoProvider, Sync>;

}  // namespace qemu
}  // namespace uart

#endif  // LIB_UART_QEMU_H_
