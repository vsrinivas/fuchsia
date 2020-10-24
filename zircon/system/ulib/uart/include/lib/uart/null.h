// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_LIB_UART_INCLUDE_LIB_UART_NULL_H_
#define ZIRCON_SYSTEM_DEV_LIB_UART_INCLUDE_LIB_UART_NULL_H_

// uart::null::Driver is a bit bucket.
// It also serves to demonstrate the API required by uart::KernelDriver.

#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <string_view>

#include "uart.h"

namespace uart {
namespace null {

struct Driver {
  struct config_type {};

  Driver() = default;

  Driver(const Driver&) = default;

  explicit Driver(const config_type& config) {}

  // API to (not) fill a ZBI item describing this UART.
  constexpr uint32_t type() const { return 0; }
  constexpr uint32_t extra() const { return 0; }
  constexpr size_t size() const { return 0; }
  void FillItem(void*) const { ZX_PANIC("should never be called"); }

  // API to (not) match a ZBI item describing this UART.
  static std::optional<Driver> MaybeCreate(const zbi_header_t&, const void*) { return {}; }

  // uart::KernelDriver UartDriver API
  //
  // Each method is a template parameterized by an IoProvider type for
  // accessing the hardware registers so that real Driver types can be used
  // with hwreg::Mock in tests independent of actual hardware access.  The
  // null Driver never uses the `io` arguments.

  template <typename IoProvider>
  void Init(IoProvider& io) {}

  // Return true if Write can make forward progress right now.
  template <typename IoProvider>
  bool TxReady(IoProvider& io) {
    return true;
  }

  // This is called only when TxReady() has just returned true.  Advance
  // the iterator at least one and as many as is convenient but not past
  // end, outputting each character before advancing.
  template <typename IoProvider, typename It1, typename It2>
  auto Write(IoProvider& io, It1 it, const It2& end) {
    return end;
  }

  // Poll for an incoming character and return one if there is one.
  template <typename IoProvider>
  std::optional<uint8_t> Read(IoProvider& io) {
    return {};
  }

  // Enable transmit interrupts so Interrupt will be called when TxReady().
  template <typename IoProvider>
  void EnableTxInterrupt(IoProvider& io) {}

  // Set the UART up to deliver interrupts.  This is called after Init.
  template <typename IoProvider>
  void InitInterrupt(IoProvider& io) {}

  // This tells the IoProvider what device resources to provide.
  constexpr config_type config() const { return {}; }

  // This tells the IoProvider whether this UartDriver wants PIO or MMIO.
  constexpr uint16_t pio_size() const { return 0; }
};

}  // namespace null

// Provide a specialization to do nothing with the lack of configuration info
// and provide no access to the lack of hardware.
template <>
class BasicIoProvider<null::Driver::config_type> {
 public:
  BasicIoProvider(const null::Driver::config_type&, uint16_t) {}

 private:
  // Nothing should call this.  The visibility will cause a compilation error.
  auto io() { return nullptr; }
};

}  // namespace uart

#endif  // ZIRCON_SYSTEM_DEV_LIB_UART_INCLUDE_LIB_UART_NULL_H_
