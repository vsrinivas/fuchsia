// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace fit {
namespace internal {

// Checks if |T| is null. Defaults to false.
// |Comparison| is the type yielded by comparing a T value with nullptr.
template <typename T, typename Comparison = bool>
struct equals_null {
    static constexpr bool test(const T&) { return false; }
};

// Partial specialization for |T| values comparable to nullptr.
template <typename T>
struct equals_null<T, decltype(*static_cast<T*>(nullptr) == nullptr)> {
    static constexpr bool test(const T& v) { return v == nullptr; }
};

template <typename T>
constexpr bool is_null(const T& v) {
    return equals_null<T>::test(v);
}

} // namespace internal
} // namespace fit
