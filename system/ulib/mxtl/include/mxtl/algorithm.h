// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mxtl/type_support.h>

namespace mxtl {

template<class T>
constexpr const T& min(const T& a, const T& b) {
    return (b < a) ? b : a;
}

template<class T>
constexpr const T& max(const T& a, const T& b) {
    return (a < b) ? b : a;
}

template<class T>
constexpr const T& clamp(const T& v, const T& lo, const T& hi) {
    return (v < lo) ? lo : (hi < v) ? hi : v;
}

// is_pow2
//
// Test to see if an unsigned integer type is an exact power of two or
// not.  Note, this needs to use a helper struct because we are not
// allowed to partially specialize functions (because C++).
namespace internal {
template <typename T, typename Enable = void> struct IsPow2Helper;

template <typename T>
struct IsPow2Helper<T, typename enable_if<is_unsigned_integer<T>::value>::type> {
    // TODO(johngro) : should 0 be a valid power of two or not?
    // On the one hand...
    // lim(X -> 0) log2(X) diverges
    //
    // On the other hand hand...
    // but lim(X -> -inf) 2^X = 0
    //
    // For now, we consider 0 to be a power of two.  Users can explicitly check
    // for zero if they need to exclude it from consideration.
    static constexpr bool is_pow2(T val) {
        return (val != 0) && (((val - 1u) & val) == 0);
    }
};
}

template <typename T>
constexpr bool is_pow2(T val) {
    return internal::IsPow2Helper<T>::is_pow2(val);
}

}  // namespace mxtl
