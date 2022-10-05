// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lib/boot-shim/uart.h"

namespace boot_shim {

fit::result<UartItem::DataZbi::Error> UartItem::AppendItems(DataZbi& zbi) const {
  return std::visit(
      [&zbi](const auto& driver) -> fit::result<DataZbi::Error> {
        if (driver.type() == 0) {
          return fit::ok();
        }

        if (auto result = zbi.Append({
                .type = driver.type(),
                .length = static_cast<uint32_t>(driver.size()),
                .extra = driver.extra(),
            });
            result.is_ok()) {
          auto& [header, payload] = *result.value();
          driver.FillItem(payload.data());
        } else {
          return result.take_error();
        }

        return fit::ok();
      },
      driver_);
}

}  // namespace boot_shim
