// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mxtl/type_support.h>
#include <stdlib.h>

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
    static constexpr bool is_pow2(T val) {
        return (val != 0) && (((val - 1u) & val) == 0);
    }
};
}

// is_pow2<T>(T val)
//
// Tests to see if val (which may be any unsigned integer type) is a power of 2
// or not.  0 is not considered to be a power of 2.
//
template <typename T>
constexpr bool is_pow2(T val) {
    return internal::IsPow2Helper<T>::is_pow2(val);
}

// roundup rounds up val until it is divisible by multiple.
// Zero is divisible by all multiples.
template<class T, class U,
         class = typename enable_if<is_unsigned_integer<T>::value>::type,
         class = typename enable_if<is_unsigned_integer<U>::value>::type>
constexpr const T roundup(const T& val, const U& multiple) {
    return val == 0 ? 0 :
            is_pow2<U>(multiple) ? (val + (multiple - 1)) & ~(multiple - 1) :
                ((val + (multiple - 1)) / multiple) * multiple;
}

// Returns a pointer to the first element that is not less than |value|, or
// |last| if no such element is found.
//
// Similar to <http://en.cppreference.com/w/cpp/algorithm/lower_bound>
template<class T, class U>
const T* lower_bound(const T* first, const T* last, const U& value) {
    while (first < last) {
        const T* probe = first + (last - first) / 2;
        if (*probe < value) {
            first = probe + 1;
        } else {
            last = probe;
        }
    }
    return last;
}

// Returns a pointer to the first element that is not less than |value|, or
// |last| if no such element is found.
//
// |comp| is used to compare the elements rather than operator<.
//
// Similar to <http://en.cppreference.com/w/cpp/algorithm/lower_bound>
template<class T, class U, class Compare>
const T* lower_bound(const T* first, const T* last, const U& value, Compare comp) {
    while (first < last) {
        const T* probe = first + (last - first) / 2;
        if (comp(*probe, value)) {
            first = probe + 1;
        } else {
            last = probe;
        }
    }
    return last;
}

template <typename T, size_t N>
constexpr size_t count_of(T const(&)[N]) {
    return N;
}

}  // namespace mxtl
