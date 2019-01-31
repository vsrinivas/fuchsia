// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace internal {

// type_size<T>() is 1 if T is (const/volatile) void or sizeof(T) otherwise.
template <typename T> inline constexpr size_t type_size() { return sizeof(T); }
template <> inline constexpr size_t type_size<void>() { return 1u; }
template <> inline constexpr size_t type_size<const void>() { return 1u; }
template <> inline constexpr size_t type_size<volatile void>() { return 1u; }
template <> inline constexpr size_t type_size<const volatile void>() { return 1u; }

}  // namespace internal
