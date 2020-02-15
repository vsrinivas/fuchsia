// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#if __cplusplus >= 201103L && __has_include(<type_traits>)
#include <type_traits>
#endif

// This represents a UTF-8 string constant provided by the vDSO itself.
// This pointer remains valid and the string doesn't change for the
// life of the process (if not the system).
//
// This type exists to be the return value type for vDSO functions.
// In current machine ABIs, it's returned "for free" in two registers.
// To a C caller, these functions have ABIs indistinguishable from if
// they simply returned `const char*` so there is no overhead to
// supporting the explicit-length API as well as the traditional C
// string API, though it does require writing out `.c_str` in the
// source.  C++ 17 callers can take advantage of direct coercion to
// the standard std::string_view and std::u8string_view types, which
// also allows e.g. direct construction of std::string.
typedef struct {
  const char* c_str;  // UTF-8, guaranteed to be '\0'-terminated.
  size_t length;      // Length, not including the '\0' terminator.

#ifdef __cplusplus
  // This is ABI-identical to the usual implementation of std::string_view,
  // when applied to NUL-terminated C strings.  But this API doesn't presume
  // that std::string_view has a particular implementation or exists at all.
  // For convenience of use without directly using the C++ standard library
  // API, a templatized implicit coercion is defined to types that have the
  // API of std::string_view or std::u8string_view.  With the most common
  // implementations, this coercion will be compiled away to nothing.
  template <
      typename _T
#if __cplusplus >= 201103L && __has_include(<type_traits>)
      ,
      typename = typename std::enable_if<sizeof(typename _T::value_type) == sizeof(char)>::type
#endif
      >
  operator _T() {
    // It's preferable to exclude incompatible types via SFINAE so that
    // the user's diagnostic experience is exactly as if no coercion
    // operator existed.  SFINAE should exclude this definition when a
    // C++11 <type_traits> is available to define std::enable_if.  If
    // no standard C++ library header is available, this will provide
    // a specific diagnostic.
    static_assert(sizeof(typename _T::value_type) == sizeof(char),
                  "zx_string_view_t can be coerced to C++ 17 std::string_view"
                  " or std::u8string_view or types with equivalent API");
    return {reinterpret_cast<typename _T::const_pointer>(c_str), length};
  }

  // Preferably zx_string_view_t values should just be coerced to
  // std::string_view.  But it provides the most minimal aspects of
  // the equivalent API in case a return value expression is used
  // directly as `zx_foo_string().data()`, for example.
  using value_type = char;
  using const_pointer = const char*;
  using size_type = size_t;
  const_pointer data() const { return c_str; }
  size_type size() const { return length; }
#endif
} zx_string_view_t;
