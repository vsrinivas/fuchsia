// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UART_ALL_H_
#define LIB_UART_ALL_H_

#include <stdio.h>
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
  template <typename... Args>
  static std::optional<DummyDriver> MaybeCreate(Args&&...) {
    return {};
  }

  void Unparse(FILE*) const { ZX_PANIC("DummyDriver should never be called!"); }
};

// std::visit is not pure-PIC-friendly.  hwreg implements a limited version
// that is pure-PIC-friendly and that works for the cases here.  Reach in and
// steal that instead of copying the implementation here, since there isn't
// any particularly good "public" place to move it into instead.
using hwreg::internal::Visit;

}  // namespace internal

namespace all {

// uart::all::WithAllDrivers<Template, Args...> instantiates the template class
// Template<Args..., foo::Driver, bar::Driver, ...> for all the drivers
// supported by this kernel build (foo, bar, ...).  Using a template that takes
// a template template parameter is the only real way (short of macros) to have
// a single list of the supported uart::xyz::Driver implementations.
template <template <class... Drivers> class Template, typename... Args>
using WithAllDrivers = Template<
    // Any additional template arguments precede the arguments for each driver.
    Args...,
    // A default-constructed variant gets the null driver.
    null::Driver,
#if defined(__aarch64__) || UART_ALL_DRIVERS
    pl011::Driver,  // TODO(fxbug.dev/49423): many more...
#endif
#if defined(__x86_64__) || defined(__i386__) || UART_ALL_DRIVERS
    ns8250::MmioDriver, ns8250::PioDriver,
#endif
    // This is never used but permits a trailing comma above.
    internal::DummyDriver>;

// The hardware support object underlying whichever KernelDriver type is the
// active variant can be extracted into this type and then used to construct a
// new uart::all::KernelDriver instantiation in a different environment.
//
// The underlying UartDriver types and ktl::variant (aka std::variant) hold
// only non-pointer data that can be transferred directly from one environment
// to another, e.g. to hand off from physboot to the kernel.
using Driver = WithAllDrivers<std::variant>;

// uart::all::KernelDriver is a variant across all the KernelDriver types.
template <template <typename> class IoProvider, typename Sync>
class KernelDriver {
 public:
  using uart_type = Driver;

  // In default-constructed state, it's the null driver.
  KernelDriver() = default;

  // It can be copy-constructed from one of the supported uart::xyz::Driver
  // types to hand off the hardware state from a different instantiation.
  template <typename T>
  explicit KernelDriver(const T& uart) : variant_(uart) {}

  // ...or from another all::KernelDriver::uart() result.
  explicit KernelDriver(const uart_type& uart) { *this = uart; }

  // Assignment is another way to reinitialize the configuration.
  KernelDriver& operator=(const uart_type& uart) {
    internal::Visit(
        [this](auto&& uart) {
          using ThisUart = std::decay_t<decltype(uart)>;
          variant_.template emplace<OneDriver<ThisUart>>(uart);
        },
        uart);
    return *this;
  }

  // If this ZBI item matches a supported driver, instantiate that driver and
  // return true.  If nothing matches, leave the existing driver (default null)
  // in place and return false.  The expected procedure is to apply this to
  // each ZBI item in order, so that the latest one wins (e.g. one appended by
  // the boot loader will supersede one embedded in the original complete ZBI).
  bool Match(const zbi_header_t& header, const void* payload) { return DoMatch(header, payload); }

  // This is like Match, but instead of matching a ZBI item, it matches a
  // string value for the "kernel.serial" boot option.
  bool Parse(std::string_view option) { return DoMatch(option); }

  // Write out a string that Parse() can read back to recreate the driver
  // state.  This doesn't preserve the driver state, only the configuration.
  void Unparse(FILE* out = stdout) const {
    Visit([out](const auto& active) { active.Unparse(out); });
  }

  // Apply f to selected driver.
  template <typename T, typename... Args>
  void Visit(T&& f, Args... args) {
    internal::Visit(std::forward<T>(f), variant_, std::forward<Args>(args)...);
  }

  // Apply f to selected driver.
  template <typename T, typename... Args>
  void Visit(T&& f, Args... args) const {
    internal::Visit(std::forward<T>(f), variant_, std::forward<Args>(args)...);
  }

  // Extract the hardware configuration and state.  The return type is const
  // just to make clear that this never returns a mutable reference like normal
  // accessors do, it always copies.
  const uart_type uart() const {
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
  WithAllDrivers<Variant> variant_;

  template <size_t I, typename... Args>
  bool TryOneMatch(Args&&... args) {
    using Try = std::variant_alternative_t<I, decltype(variant_)>;
    if (auto driver = Try::uart_type::MaybeCreate(std::forward<Args>(args)...)) {
      variant_.template emplace<I>(*driver);
      return true;
    }
    return false;
  }

  template <size_t... I, typename... Args>
  bool DoMatchHelper(std::index_sequence<I...>, Args&&... args) {
    return (TryOneMatch<I>(std::forward<Args>(args)...) || ...);
  }

  template <typename... Args>
  bool DoMatch(Args&&... args) {
    constexpr auto n = std::variant_size_v<decltype(variant_)>;
    return DoMatchHelper(std::make_index_sequence<n>(), std::forward<Args>(args)...);
  }
};

}  // namespace all
}  // namespace uart

#endif  // LIB_UART_ALL_H_
