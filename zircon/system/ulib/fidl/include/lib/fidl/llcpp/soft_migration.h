// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_SOFT_MIGRATION_H_
#define LIB_FIDL_LLCPP_SOFT_MIGRATION_H_

#include <type_traits>

#ifdef FIDL_LLCPP_ALLOW_DEPRECATED_RAW_CHANNELS
#define FIDL_DEPRECATED_USE_TYPED_CHANNELS
#else
#define FIDL_DEPRECATED_USE_TYPED_CHANNELS                                                \
  [[deprecated(                                                                           \
      "[fidl][llcpp] This declaration is deprecated because it uses raw |zx::channel|s. " \
      "Consider migrating to a version with typed channels (fxbug.dev/65212). "           \
      "See documentation on the declaration for details.")]]
#endif  // FIDL_LLCPP_ALLOW_DEPRECATED_RAW_CHANNELS

#ifdef FIDL_LLCPP_ALLOW_DEPRECATED_TRY_DISPATCH
#define FIDL_EMIT_STATIC_ASSERT_ERROR_FOR_TRY_DISPATCH(FidlProtocol) (void(0))
#else
// |std::is_void| delays the evaluation of the static assert until the user
// calls |WireTryDispatch|, thus pushing deprecation errors to use-site instead
// of the |WireTryDispatch| definition.
#define FIDL_EMIT_STATIC_ASSERT_ERROR_FOR_TRY_DISPATCH(FidlProtocol)                            \
  static_assert(std::is_void<FidlProtocol>::value,                                              \
                "[fidl][llcpp] |fidl::WireTryDispatch<Protocol>| is deprecated because it "     \
                "deviates from normal handling of unknown FIDL methods. Consider migrating to " \
                "|fidl::WireDispatch<Protocol>|. See fxbug.dev/85473 for details.")
#endif  // FIDL_LLCPP_ALLOW_DEPRECATED_TRY_DISPATCH

#ifdef FIDL_LLCPP_ALLOW_DEPRECATED_RAW_CHANNELS
#define FIDL_CONDITIONALLY_EXPLICIT_CONVERSION
#else
#define FIDL_CONDITIONALLY_EXPLICIT_CONVERSION explicit
#endif  // FIDL_LLCPP_ALLOW_DEPRECATED_RAW_CHANNELS

#endif  // LIB_FIDL_LLCPP_SOFT_MIGRATION_H_
