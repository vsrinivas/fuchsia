// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_BOOT_SHIM_H_
#define ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_BOOT_SHIM_H_

#include <lib/stdcompat/span.h>
#include <stdio.h>

#include <array>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "item-base.h"

namespace boot_shim {

// See the BootShim template class, below.
class BootShimBase : public ItemBase {
 public:
  constexpr BootShimBase(const BootShimBase&) = default;

  explicit constexpr BootShimBase(const char* shim_name, FILE* log = stdout)
      : shim_name_(shim_name), log_(log) {}

  const char* shim_name() const { return shim_name_; }

  FILE* log() const { return log_; }

  bool Check(const char* what, fit::result<std::string_view> result) const;

  bool Check(const char* what, fit::result<InputZbi::Error> result) const;

  bool Check(const char* what, fit::result<InputZbi::CopyError<WritableBytes>> result) const;

  bool Check(const char* what, fit::result<DataZbi::Error> result) const;

 protected:
  class Cmdline : public ItemBase {
   public:
    enum Index : size_t { kName, kInfo, kBuildId, kLegacy, kCount };

    constexpr std::string_view& operator[](Index i) { return chunks_[i]; }

    constexpr std::string_view operator[](Index i) const { return chunks_[i]; }

    void set_strings(cpp20::span<std::string_view> strings) { strings_ = strings; }
    void set_cstr(cpp20::span<const char*> cstr) { cstr_ = cstr; }

    size_t size_bytes() const;
    fit::result<DataZbi::Error> AppendItems(DataZbi& zbi) const;

   private:
    size_t Collect(std::optional<WritableBytes> payload = std::nullopt) const;

    std::array<std::string_view, kCount> chunks_;
    cpp20::span<std::string_view> strings_;
    cpp20::span<const char*> cstr_;
  };

  void Log(const Cmdline& cmdline, ByteView ramdisk) const;

 private:
  const char* shim_name_;
  FILE* log_;
};

// BootShim is a base class for implementing boot shims.  The model is that the
// shim starts up, collects data from whatever legacy sources it has, then
// ingests the ZBI, then appends "bootloader-provided" items to the data ZBI.
// This class manages the data collection and tracks what items to append.
//
// Each template argument is a class implementing the boot_shim::ItemBase API.
// Each "item" can produce zero, one, or more ZBI items at runtime.
//
// In several shims, everything must be figured out so that the final image
// sizes are all known before any memory allocation can be done.  So first a
// data collection pass stores everything it needs in each item object.  (The
// BootShim base class does not do this part except for the CMDLINE item.)
//
// Then BootShim::size_bytes() on BootShim sums Items::size_bytes() so the shim
// can reserve memory for the data ZBI.  Once the shim has ingested the input
// ZBI and set up memory allocation it can set up the data ZBI with as much
// extra capacity as size_bytes() requested.  Then BootShim::AppendItems
// iterates across Items::AppendItems calls.  The shim is now ready to boot.
template <class... Items>
class BootShim : public BootShimBase {
 public:
  // Move-only, not default-constructible.
  BootShim() = delete;
  BootShim(const BootShim&) = delete;
  constexpr BootShim(BootShim&&) noexcept = default;
  constexpr BootShim& operator=(const BootShim&) = delete;
  constexpr BootShim& operator=(BootShim&&) noexcept = default;

  // The caller must supply the shim's own program name as a string constant.
  // This string is used in log messages and in "bootloader.name=...".  In real
  // phys shims, this is always Symbolize::kProgramName_ and stdout is the only
  // FILE* there is.  Other log streams can be used in testing.
  explicit constexpr BootShim(const char* shim_name, FILE* log = stdout)
      : BootShimBase(shim_name, log) {
    Get<Cmdline>()[Cmdline::kName] = shim_name;
  }

  // These fluent setters contribute to the built-in ZBI_TYPE_CMDLINE item,
  // along with the mandatory shim name from the constructor.

  constexpr BootShim& set_info(std::string_view str) {
    Get<Cmdline>()[Cmdline::kInfo] = str;
    return *this;
  }

  constexpr BootShim& set_build_id(std::string_view str) {
    Get<Cmdline>()[Cmdline::kBuildId] = str;
    return *this;
  }

  constexpr BootShim& set_cmdline(std::string_view str) {
    // Remove any incoming trailing NULs, just in case.
    if (auto pos = str.find_last_not_of('\0'); pos != std::string_view::npos) {
      Get<Cmdline>()[Cmdline::kLegacy] = str.substr(0, pos + 1);
    }
    return *this;
  }

  constexpr BootShim& set_cmdline_strings(cpp20::span<std::string_view> strings) {
    Get<Cmdline>().set_strings(strings);
    return *this;
  }

  constexpr BootShim& set_cmdline_cstrings(cpp20::span<const char*> cstrings) {
    Get<Cmdline>().set_cstr(cstrings);
    return *this;
  }

  // Log how things look after calling set_* methods.
  void Log(ByteView ramdisk) const { BootShimBase::Log(Get<Cmdline>(), ramdisk); }

  // Return the total size (upper bound) of additional data ZBI items.
  constexpr size_t size_bytes() const {
    return OnItems([](auto&... items) { return (items.size_bytes() + ...); });
  }

  // Append additional items to the data ZBI.  The caller ensures there is as
  // much spare capacity as size_bytes() previously returned.
  fit::result<DataZbi::Error> AppendItems(DataZbi& zbi) {
    fit::result<DataZbi::Error> result = fit::ok();
    auto append = [&zbi, &result](auto& item) {
      result = item.AppendItems(zbi);
      return result.is_error();
    };
    return AnyItem(append) ? result : fit::ok();
  }

  // Get the item object of a particular type (among Items).
  template <typename T>
  constexpr T& Get() {
    static_assert(std::is_same_v<T, Cmdline> || (std::is_same_v<T, Items> || ...));
    return std::get<T>(items_);
  }
  template <typename T>
  constexpr const T& Get() const {
    static_assert(std::is_same_v<T, Cmdline> || (std::is_same_v<T, Items> || ...));
    return std::get<T>(items_);
  }

  // This calls item.Init(args..., shim_name(), log()).
  template <class Item, typename... Args>
  decltype(auto) InitItem(Item& item, Args&&... args) {
    return item.Init(std::forward<Args>(args)..., shim_name(), log());
  }

  // This calls the Get<Item>() item method Init(args..., shim_name(), log()).
  template <class Item, typename... Args>
  decltype(auto) InitGetItem(Args&&... args) {
    return InitItem(Get<Item>(), std::forward<Args>(args)...);
  }

  // Returns callback(Items&...).
  template <typename T>
  constexpr decltype(auto) OnItems(T&& callback) {
    static_assert(std::is_invocable_v<T, Cmdline&, Items&...>);
    return std::apply(std::forward<T>(callback), items_);
  }
  template <typename T>
  constexpr decltype(auto) OnItems(T&& callback) const {
    static_assert(std::is_invocable_v<T, Cmdline&, Items&...>);
    return std::apply(std::forward<T>(callback), items_);
  }

  // Calls callback(item) for each of Items.
  // If Base is given, the items not derived from Base are skipped.
  template <typename T, typename Base = void>
  constexpr void ForEachItem(T&& callback) {
    OnItems([&](auto&... item) {
      (((std::is_void_v<Base> || std::is_base_of_v<Base, std::decay_t<decltype(item)>>)
            ? (void)callback(item)
            : (void)false),
       ...);
    });
  }
  template <typename T, typename Base = void>
  constexpr void ForEachItem(T&& callback) const {
    OnItems([&](auto&... item) {
      (((std::is_void_v<Base> || std::is_base_of_v<Base, std::decay_t<decltype(item)>>)
            ? (void)callback(item)
            : (void)false),
       ...);
    });
  }

  // Returns callback(item) && ... for each of Items.
  // If Base is given, the items not derived from Base are skipped.
  template <typename T, typename Base = void>
  constexpr bool EveryItem(T&& callback) {
    return OnItems([&](auto&... item) {
      return (((std::is_void_v<Base> || std::is_base_of_v<Base, std::decay_t<decltype(item)>>)
                   ? callback(item)
                   : true) &&
              ...);
    });
  }
  template <typename T, typename Base = void>
  constexpr bool EveryItem(T&& callback) const {
    return OnItems([&](auto&... item) {
      return (((std::is_void_v<Base> || std::is_base_of_v<Base, std::decay_t<decltype(item)>>)
                   ? callback(item)
                   : true) &&
              ...);
    });
  }

  // Returns callback(item) || ... for each of Items.
  // If Base is given, the items not derived from Base are skipped.
  template <typename T, typename Base = void>
  constexpr bool AnyItem(T&& callback) {
    return OnItems([&](auto&... item) {
      return (((std::is_void_v<Base> || std::is_base_of_v<Base, std::decay_t<decltype(item)>>)  //
                   ? callback(item)
                   : false) ||
              ...);
    });
  }
  template <typename T, typename Base = void>
  constexpr bool AnyItem(T&& callback) const {
    return OnItems([&](auto&... item) {
      return (((std::is_void_v<Base> || std::is_base_of_v<Base, std::decay_t<decltype(item)>>)  //
                   ? callback(item)
                   : false) ||
              ...);
    });
  }

 private:
  std::tuple<Cmdline, Items...> items_;
};

}  // namespace boot_shim

#endif  // ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_BOOT_SHIM_H_
