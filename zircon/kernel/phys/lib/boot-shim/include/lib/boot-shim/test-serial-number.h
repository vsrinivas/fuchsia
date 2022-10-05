// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_TEST_SERIAL_NUMBER_H_
#define ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_TEST_SERIAL_NUMBER_H_

#include <lib/fit/result.h>

#include <string_view>

#include "item-base.h"

namespace boot_shim {

// This "item" optionally provides a synthetic ZBI_TYPE_SERIAL_NUMBER item
// when instructed by a special command-line argument found in the input ZBI.
// Some tests use the special argument `bootloader.zbi.serial-number=...` to
// ensure that a ZBI_TYPE_SERIAL_NUMBER will be present even if there isn't
// one supplied by the boot loader, as is the case under generic emulation.
class TestSerialNumberItem : public ItemBase {
 public:
  // Scan the ZBI-embedded command line switches for one meant specifically to
  // tell the shim to synthesize a ZBI_TYPE_SERIAL_NUMBER item.
  fit::result<InputZbi::Error> Init(InputZbi zbi);

  constexpr size_t size_bytes() const { return ItemSize(payload_.size_bytes()); }

  fit::result<DataZbi::Error> AppendItems(DataZbi& zbi) const;

 private:
  ByteView payload_;
};

}  // namespace boot_shim

#endif  // ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_TEST_SERIAL_NUMBER_H_
