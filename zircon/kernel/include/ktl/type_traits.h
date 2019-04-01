// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <type_traits>

namespace ktl {

template <typename T>
using is_const = std::is_const<T>;

template <typename T>
using is_lvalue_reference = std::is_lvalue_reference<T>;

template <typename T>
using is_pod = std::is_pod<T>;

template <typename T, typename U>
using is_same = std::is_same<T, U>;

template <typename T>
using remove_const = std::remove_const<T>;

template <typename T>
using remove_reference = std::remove_reference<T>;

} // namespace ktl
