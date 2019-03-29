// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ktl/type_traits.h>

namespace ktl {

template <typename T>
constexpr T&& forward(typename remove_reference<T>::type& t) {
    return static_cast<T&&>(t);
}

template <typename T>
constexpr T&& forward(typename remove_reference<T>::type&& t) {
    static_assert(!is_lvalue_reference<T>::value, "bad fbl::forward call");
    return static_cast<T&&>(t);
}

} // namespace ktl
