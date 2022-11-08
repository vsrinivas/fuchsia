// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_DEVICETREE_BOOT_SHIM_H_
#define ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_DEVICETREE_BOOT_SHIM_H_

#include <lib/devicetree/devicetree.h>
#include <lib/devicetree/matcher.h>
#include <lib/fit/result.h>

#include <type_traits>
#include <utility>

#include <fbl/macros.h>

#include "boot-shim.h"

namespace boot_shim {

namespace internal {

// Checks whether an Item implements the |InitMatcher| which returns a Matcher item to
// |devicetree::Match| during the |InitItem| phase.
DECLARE_HAS_MEMBER_FN(HasGetInitMatcher, GetInitMatcher);

template <typename T, typename = void>
struct IsDevicetreeItemImpl : std::false_type {};

// Need separate specialization so we dont try to check |item.GetInitMatcher return type| on an item
// that doesnt have that method.
template <typename T>
struct IsDevicetreeItemImpl<T, typename std::enable_if<HasGetInitMatcher_v<T>>::type> {
  static constexpr bool value =
      devicetree::kIsValidMatcher<decltype(std::declval<T>().GetInitMatcher())>;
};

// Alias is needed, otherwise Predicate<T> fails to match the template IsValidMatcherImpl<T,
// typename>.
template <typename T>
using IsDevicetreeItem = IsDevicetreeItemImpl<T>;

}  // namespace internal

// A DevicetreeBootShim represents a collection of items, some of which may be interested in
// looking into the devicetree itself to gather information.
//
// DevicetreeItem requires inspecting the devicetree. In addition to the API requirements from a
// BootShim's item, it must:
//  * Implement |GetInitMatcher|
//  * Return type of |GetInitMatcher| must satisfy |devicetree::kIsValidMatcher<T>|.
//
// The `InitMatcher`s are provided as arguments to |devicetree::Match| call when
// |DevicetreeBootShim::InitDevicetreeItems()| is called.
template <typename... Items>
class DevicetreeBootShim : public BootShim<Items...> {
  using Base = BootShim<Items...>;

 public:
  explicit DevicetreeBootShim(const char* name, devicetree::Devicetree dt, FILE* log = stdout)
      : Base(name, log), dt_(dt) {}

  // Initializes all devicetree boot shim items.
  fit::result<size_t, size_t> InitDevicetreeItems() {
    auto match_with = [this](auto&... items) {
      return devicetree::Match(dt_, items.GetInitMatcher()...);
    };
    return Base::template OnSelectItems<internal::IsDevicetreeItem>(match_with);
  }

 private:
  devicetree::Devicetree dt_;
};

}  // namespace boot_shim

#endif  // ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_DEVICETREE_BOOT_SHIM_H_
