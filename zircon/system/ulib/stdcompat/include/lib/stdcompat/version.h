// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STDCOMPAT_VERSION_H_
#define LIB_STDCOMPAT_VERSION_H_

// This <version> polyfills is meant to provide the feature testing macros for the rest of
// the stdcompat library. It is not meant to be a full polyfill of <version>.

#if __has_include(<version>)
#include <version>
#elif __cplusplus > 201703L
#error "cpp=std20 must provide a '<version>' header."
#else

#if __has_include(<string_view>) && !defined(__cpp_lib_string_view) && __cplusplus >= 201606L
#define __cpp_lib_string_view 201606L
#endif

#endif

#endif  // LIB_STDCOMPAT_VERSION_H_
