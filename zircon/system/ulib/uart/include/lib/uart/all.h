// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_LIB_UART_INCLUDE_LIB_UART_ALL_H_
#define ZIRCON_SYSTEM_DEV_LIB_UART_INCLUDE_LIB_UART_ALL_H_

#include <zircon/boot/image.h>

#include <utility>
#include <variant>

#include <hwreg/internal.h>

#include "ns8250.h"
#include "null.h"
#include "pl011.h"
#include "uart.h"

namespace uart {

namespace internal {

struct DummyDriver : public null::Driver {
  static std::optional<DummyDriver> MaybeCreate(const zbi_header_t&, const void*) { return {}; }
};

// std::visit is not pure-PIC-friendly.  hwreg implements a limited version
// that is pure-PIC-friendly and that works for the cases here.  Reach in and
// steal that instead of copying the implementation here, since there isn't
// any particularly good "public" place to move it into instead.
using hwreg::internal::Visit;

}  // namespace internal

namespace all {

// uart::all::Driver<T, ...> instantiates T<..., foo::Driver, bar::Driver, ...>
// for all the drivers supported by this kernel build.  Using a template that
// takes a template template parameter is the only real way (short of macros)
// to have a single list of the supported uart::xyz::Driver implementations.
template <template <class... Drivers> class T, typename... Args>
using Driver = T<Args...,
                 // A default-constructed variant gets the null driver.
                 null::Driver,
#ifdef __aarch64__
                 pl011::Driver,  // TODO(fxbug.dev/49423): many more...
#endif
#if defined(__x86_64__) || defined(__i386__)
                 ns8250::MmioDriver, ns8250::PioDriver,
#endif
                 // This is never used but permits a trailing comma above.
                 internal::DummyDriver>;

// uart::all::KernelDriver is a variant across all the KernelDriver types.
template <template <typename> class IoProvider, typename Sync>
class KernelDriver {
 public:
  // The hardware support object underlying whichever KernelDriver type is the
  // active variant can be extracted into this type and then used to construct
  // a new uart::all::KernelDriver instantiation in a different environment.
  //
  // The underlying UartDriver types and ktl::variant (aka std::variant) hold
  // only non-pointer data that can be transferred directly from one
  // environment to another, e.g. to hand off from physboot to the kernel.
  using uart_type = Driver<std::variant>;

  // In default-constructed state, it's the null driver.
  KernelDriver() = default;

  // It can be copy-constructed from one of the supported uart::xyz::Driver
  // types to hand off the hardware state from a different instantiation.
  template <typename T>
  explicit KernelDriver(const T& uart) : variant_(uart) {}

  // ...or from another all::KernelDriver::uart() result.
  explicit KernelDriver(const uart_type& uart) {
    internal::Visit(
        [this](auto&& uart) {
          using ThisUart = std::decay_t<decltype(uart)>;
          variant_.template emplace<OneDriver<ThisUart>>(uart);
        },
        uart);
  }

  // If this ZBI item matches a supported driver, instantiate that driver and
  // return true.  If nothing matches, leave the existing driver (default null)
  // in place and return false.  The expected procedure is to apply this to
  // each ZBI item in order, so that the latest one wins (e.g. one appended by
  // the boot loader will supersede one embedded in the original complete ZBI).
  bool Match(const zbi_header_t& header, const void* payload) {
    constexpr auto n = std::variant_size_v<decltype(variant_)>;
    return DoMatch(std::make_index_sequence<n>(), header, payload);
  }

  // Apply f to selected driver.
  template <typename T, typename... Args>
  void Visit(T&& f, Args... args) {
    internal::Visit(std::forward<T>(f), variant_, std::forward<Args>(args)...);
  }

  // Extract the hardware configuration and state.
  uart_type uart() {
    uart_type driver;
    Visit([&driver](auto&& active) {
      const auto& uart = active.uart();
      driver.emplace<std::decay_t<decltype(uart)>>(uart);
    });
    return driver;
  }

 private:
  template <class Uart>
  using OneDriver = uart::KernelDriver<Uart, IoProvider, Sync>;
  template <class... Uart>
  using Variant = std::variant<OneDriver<Uart>...>;
  Driver<Variant> variant_;

  template <size_t I>
  bool TryOneMatch(const zbi_header_t& header, const void* payload) {
    using Try = std::variant_alternative_t<I, decltype(variant_)>;
    if (auto driver = Try::uart_type::MaybeCreate(header, payload)) {
      variant_.template emplace<I>(*driver);
      return true;
    }
    return false;
  }

  template <size_t... I>
  bool DoMatch(std::index_sequence<I...>, const zbi_header_t& header, const void* payload) {
    return (TryOneMatch<I>(header, payload) || ...);
  }
};

}  // namespace all
}  // namespace uart

#endif  // ZIRCON_SYSTEM_DEV_LIB_UART_INCLUDE_LIB_UART_ALL_H_
