// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_ITEM_BASE_H_
#define ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_ITEM_BASE_H_

#include <lib/fitx/result.h>
#include <lib/stdcompat/span.h>
#include <lib/zbitl/image.h>
#include <lib/zbitl/view.h>
#include <zircon/boot/image.h>

#include <cstdint>

namespace boot_shim {

// This is the base class and API model for item types used with BootShim.
// Each derived class defines its own size_bytes and AppendItems methods.
class ItemBase {
 public:
  using ByteView = zbitl::ByteView;
  using WritableBytes = cpp20::span<std::byte>;
  using InputZbi = zbitl::View<ByteView>;
  using DataZbi = zbitl::Image<WritableBytes>;

  // Convenience used in size_bytes() implementations.
  static constexpr size_t ItemSize(size_t payload) {
    return payload == 0 ? 0 : sizeof(zbi_header_t) + ZBI_ALIGN(payload);
  }

  // These methods are not actually defined in the base class.
  // They must be defined in each derived class.

  // Return the total size (upper bound) of additional data ZBI items.
  size_t size_bytes() const;

  // Append additional items to the data ZBI.  The caller ensures there is as
  // much spare capacity as size_bytes() previously returned.
  fitx::result<DataZbi::Error> AppendItems(DataZbi& zbi) const;
};

// This defines a simple item with a flat payload already in memory.
template <uint32_t Type>
class SingleItem : public ItemBase {
 public:
  constexpr size_t size_bytes() const { return ItemSize(payload_.size_bytes()); }

  constexpr SingleItem& set_payload(ByteView payload) {
    payload_ = payload;
    return *this;
  }

  fitx::result<DataZbi::Error> AppendItems(DataZbi& zbi) const {
    if (!payload_.empty()) {
      auto result = zbi.Append({.type = Type}, payload_);
      if (result.is_error()) {
        return result.take_error();
      }
    }
    return fitx::ok();
  }

 private:
  ByteView payload_;
};

}  // namespace boot_shim

#endif  // ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_ITEM_BASE_H_
