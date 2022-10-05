// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-options/word-view.h>
#include <lib/boot-shim/test-serial-number.h>
#include <lib/stdcompat/string_view.h>
#include <zircon/boot/image.h>

namespace boot_shim {

// If "bootloader.zbi.serial-number=foo" appears in a command line item in the
// ZBI, then we'll synthesize a ZBI_TYPE_SERIAL_NUMBER item containing "foo".
constexpr std::string_view kSerialNumberEq = "bootloader.zbi.serial-number=";

fit::result<ItemBase::InputZbi::Error> TestSerialNumberItem::Init(ItemBase::InputZbi zbi) {
  ByteView found;
  for (auto [header, payload] : zbi) {
    switch (header->type) {
      case ZBI_TYPE_SERIAL_NUMBER:
        // There's a real serial number here, so don't synthesize one.
        zbi.ignore_error();
        return fit::ok();

      case ZBI_TYPE_CMDLINE:
        std::string_view line{
            reinterpret_cast<const char*>(payload.data()),
            payload.size(),
        };
        for (std::string_view word : WordView(line)) {
          if (cpp20::starts_with(word, kSerialNumberEq)) {
            word.remove_prefix(kSerialNumberEq.size());
            found = {
                reinterpret_cast<const std::byte*>(word.data()),
                word.size(),
            };
          }
        }
    }
  }
  payload_ = found;
  return zbi.take_error();
}

fit::result<ItemBase::DataZbi::Error> TestSerialNumberItem::AppendItems(
    ItemBase::DataZbi& zbi) const {
  if (!payload_.empty()) {
    auto result = zbi.Append({.type = ZBI_TYPE_SERIAL_NUMBER}, payload_);
    if (result.is_error()) {
      return result.take_error();
    }
  }
  return fit::ok();
}

}  // namespace boot_shim
