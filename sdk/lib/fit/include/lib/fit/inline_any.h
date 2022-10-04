// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_INCLUDE_LIB_FIT_INLINE_ANY_H_
#define LIB_FIT_INCLUDE_LIB_FIT_INLINE_ANY_H_

#include <lib/stdcompat/type_traits.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <type_traits>
#include <utility>

#include "internal/inline_any.h"
#include "internal/utility.h"
#include "traits.h"

namespace fit {

// |inline_any| is a polymorphic container used to implement type erasure.
//
// |inline_any| is movable and copyable, and any type placed inside of it must
// also be movable and copyable.
//
// A default constructed |inline_any| does not hold a value (it is empty).
// Moving from an |inline_any| resets it back to the empty state.
//
// It is similar to |std::any|, with the following notable differences:
// * The contained object must implement (i.e. be a subclass of) |Interface|.
// * It will never heap allocate.
//
// This avoids additional memory allocations while using a virtual interface.
// |Reserve| must be larger than the sizes of all of the individual |Interface|
// implementations.
template <typename Interface, size_t Reserve = sizeof(Interface), size_t Align = alignof(Interface)>
class inline_any : public internal::inline_any_impl<Interface, Reserve, Align,
                                                    internal::inline_any_is_pinned::no> {
 public:
  using base =
      internal::inline_any_impl<Interface, Reserve, Align, internal::inline_any_is_pinned::no>;
  using base::base;
  using base::operator=;

  inline_any(const inline_any&) = default;
  inline_any& operator=(const inline_any&) = default;

  inline_any(inline_any&&) noexcept = default;
  inline_any& operator=(inline_any&&) noexcept = default;
};

// |pinned_inline_any| is a polymorphic container used to implement type
// erasure. Unlike |inline_any|, |pinned_inline_any| cannot be moved or copied,
// but it can hold non-movable types, such as types that lend out internal
// pointers. See also |inline_any|.
//
// Since |pinned_inline_any| contents cannot be moved or copy assigned, there
// are only two ways to initialize a |pinned_inline_any|:
// - At construction time: pass |cpp17::in_place_type_t<T>| to select the
//   in-place constructor.
// - After default construction: via |emplace<T>|, whose arguments are forwarded
//   to the |T| constructor.
template <typename Interface, size_t Reserve = sizeof(Interface), size_t Align = alignof(Interface)>
class pinned_inline_any : public internal::inline_any_impl<Interface, Reserve, Align,
                                                           internal::inline_any_is_pinned::yes> {
 public:
  using base =
      internal::inline_any_impl<Interface, Reserve, Align, internal::inline_any_is_pinned::yes>;
  using base::base;
  using base::operator=;

  pinned_inline_any(const pinned_inline_any&) = delete;
  pinned_inline_any& operator=(const pinned_inline_any&) = delete;

  pinned_inline_any(pinned_inline_any&&) = delete;
  pinned_inline_any& operator=(pinned_inline_any&&) = delete;
};

}  // namespace fit

#endif  // LIB_FIT_INCLUDE_LIB_FIT_INLINE_ANY_H_
