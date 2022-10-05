// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_ITEM_BASE_H_
#define ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_ITEM_BASE_H_

#include <lib/fit/result.h>
#include <lib/stdcompat/span.h>
#include <lib/zbitl/image.h>
#include <lib/zbitl/view.h>
#include <zircon/boot/image.h>

#include <cstdint>
#include <optional>
#include <type_traits>
#include <variant>

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
    return payload == 0 ? 0 : sizeof(zbi_header_t) + ZBI_ALIGN(static_cast<uint32_t>(payload));
  }

  // These methods are not actually defined in the base class.
  // They must be defined in each derived class.

  // Return the total size (upper bound) of additional data ZBI items.
  size_t size_bytes() const;

  // Append additional items to the data ZBI.  The caller ensures there is as
  // much spare capacity as size_bytes() previously returned.
  fit::result<DataZbi::Error> AppendItems(DataZbi& zbi) const;
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

  fit::result<DataZbi::Error> AppendItems(DataZbi& zbi) const {
    if (!payload_.empty()) {
      auto result = zbi.Append({.type = Type}, payload_);
      if (result.is_error()) {
        return result.take_error();
      }
    }
    return fit::ok();
  }

 private:
  ByteView payload_;
};

// This defines a simple item with a payload stored directly in this object.
template <typename Payload, uint32_t Type, uint32_t Extra = 0>
class SingleOptionalItem : public ItemBase {
 public:
  static_assert(zbitl::is_uniquely_representable_pod_v<Payload>);

  constexpr size_t size_bytes() const { return payload_ ? ItemSize(sizeof(*payload_)) : 0; }

  constexpr SingleOptionalItem& set_payload(const Payload& payload) {
    payload_ = payload;
    return *this;
  }

  constexpr SingleOptionalItem& set_payload() {
    payload_ = std::nullopt;
    return *this;
  }

  fit::result<DataZbi::Error> AppendItems(DataZbi& zbi) const {
    return payload_ ? zbi.Append(
                          {
                              .type = Type,
                              .extra = Extra,
                          },
                          zbitl::AsBytes(*payload_))
                    : fit::ok();
  }

 private:
  std::optional<Payload> payload_;
};

// This is a base class for defining simple item types that store their data
// directly and can handle a fixed set of alternative payload types.  The
// different Payload... template parameters give different types, e.g.
// zbi_dcfg_this_t and zbi_dcfg_that_t.  The derived class Item must then define an
// overload for each Payload type:
// ```
//   static constexpr zbi_header_t ItemHeader(const zbi_dcfg_this_t& cfg) {
//     return {.type = ZBI_TYPE_KERNEL_DRIVER, .extra = ZBI_KERNEL_DRIVER_THIS};
//   }
//   static constexpr zbi_header_t ItemHeader(const zbi_dcfg_that_t& cfg) {
//     return {.type = ZBI_TYPE_KERNEL_DRIVER, .extra = ZBI_KERNEL_DRIVER_THAT};
//   }
// ```
// Calling set_payload with either a zbi_dcfg_this_t or zbi_dcfg_that_t argument
// installs a value.  The ItemHeader overload corresponding to the type of the
// argument to set_payload will be used to produce the ZBI item.  set_payload
// with no arguments resets it to initial state so no item is produced.
template <class Item, typename... Payload>
class SingleVariantItemBase : public ItemBase {
 public:
  using AnyPayload = std::variant<Payload...>;

  static_assert((zbitl::is_uniquely_representable_pod_v<Payload> && ...));

  constexpr size_t size_bytes() const { return SizeBytes(payload_); }

  constexpr Item& set_payload(const AnyPayload& payload) {
    std::visit([this](const auto& payload) { payload_ = payload; }, payload);
    return static_cast<Item&>(*this);
  }

  constexpr Item& set_payload() {
    payload_ = std::monostate{};
    return static_cast<Item&>(*this);
  }

  fit::result<DataZbi::Error> AppendItems(DataZbi& zbi) const {
    return std::visit([&zbi](const auto& payload) { return Append(zbi, payload); }, payload_);
  }

 protected:
  // The derived class defines overloads for each Payload... type.
  static constexpr zbi_header_t ItemHeader(std::monostate none) { return {}; }

  static constexpr size_t SizeBytes(std::monostate none) { return 0; }

  template <typename T>
  static constexpr size_t SizeBytes(const T& payload) {
    return ItemSize(sizeof(payload));
  }

  static constexpr fit::result<DataZbi::Error> Append(DataZbi& zbi, std::monostate none) {
    return fit::ok();
  }

  template <typename T>
  static constexpr fit::result<DataZbi::Error> Append(DataZbi& zbi, const T& payload) {
    static_assert((std::is_same_v<T, Payload> || ...));
    return zbi.Append(Item::ItemHeader(payload), zbitl::AsBytes(payload));
  }

  template <typename... Args>
  static constexpr fit::result<DataZbi::Error> Append(Args&&... args) {
    static_assert((std::is_void_v<Args> && ...));
    return fit::ok();
  }

 private:
  std::variant<std::monostate, Payload...> payload_;
};

}  // namespace boot_shim

#endif  // ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_ITEM_BASE_H_
