// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_BIT_H_
#define LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_BIT_H_

#include <limits>

#if __has_include(<bit>)
#include <bit>
#endif

#include "memory.h"

namespace cpp20 {

#if __cpp_lib_bit_cast >= 201806L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

using std::bit_cast;

#else  // Use builtin to provide constexpr bit_cast.

#if defined(__has_builtin) && __has_builtin(__builtin_bit_cast) && \
    !defined(LIB_STDCOMPAT_NO_BUILTIN_BITCAST)

template <typename To, typename From>
constexpr std::enable_if_t<sizeof(To) == sizeof(From) && std::is_trivially_copyable<To>::value &&
                               std::is_trivially_copyable<From>::value,
                           To>
bit_cast(const From& from) {
  return __builtin_bit_cast(To, from);
}

#else  // Use memcpy instead, not constexpr though.

#define LIB_STDCOMPAT_NONCONSTEXPR_BITCAST 1

template <typename To, typename From>
std::enable_if_t<sizeof(To) == sizeof(From) && std::is_trivially_copyable<To>::value &&
                     std::is_trivially_copyable<From>::value,
                 To>
bit_cast(const From& from) {
  std::aligned_storage_t<sizeof(To)> uninitialized_to;
  memcpy(static_cast<void*>(&uninitialized_to), static_cast<const void*>(cpp17::addressof(from)),
         sizeof(To));
  return *reinterpret_cast<const To*>(&uninitialized_to);
}

#endif  //  defined(__has_builtin) && __has_builtin(__builtin_bit_cast)

#endif  //  __cpp_lib_bit_cast >= 201806L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

}  // namespace cpp20

#endif  // LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_BIT_H_
