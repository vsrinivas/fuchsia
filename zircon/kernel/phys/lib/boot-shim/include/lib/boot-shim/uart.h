// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_UART_H_
#define ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_UART_H_

#include <lib/uart/all.h>

#include "item-base.h"

namespace boot_shim {

// This can supply a ZBI_TYPE_KERNEL_DRIVER item based on the UART driver configuration.
class UartItem : public boot_shim::ItemBase {
 public:
  void Init(const uart::all::Driver& uart) { driver_ = uart; }

  constexpr size_t size_bytes() const { return ItemSize(zbi_dcfg_size()); }

  fit::result<DataZbi::Error> AppendItems(DataZbi& zbi) const;

 private:
  constexpr size_t zbi_dcfg_size() const {
    return std::visit([](const auto& driver) { return driver.size(); }, driver_);
  }

  uart::all::Driver driver_;
};

}  // namespace boot_shim

#endif  // ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_UART_H_
