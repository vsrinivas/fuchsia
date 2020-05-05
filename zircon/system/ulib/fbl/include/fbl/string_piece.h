// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_STRING_PIECE_H_
#define FBL_STRING_PIECE_H_

#include <string.h>
#include <zircon/compiler.h>

#include <string_view>
#include <type_traits>

#include <fbl/string_traits.h>

namespace fbl {

constexpr static size_t constexpr_strlen(const char* str) {
#if defined(_MSC_VER)
#error "__builtin_strlen not defined for MSVC++"
#else
  return __builtin_strlen(str);
#endif
}

// A string-like object that points to a sized piece of memory.
//
// fbl::StringPiece is an extension of std::string_view. Beyond the standard API,
// it only adds conversions from fbl's string-like types and from nullptr.
//
// |length()| does NOT include a trailing NUL and no guarantee is made that
// you can check |data()[length()]| to see if a NUL is there.
//
// Basically, these aren't C strings, don't think otherwise.
// The string piece does not own the data it points to.

class StringPiece : public std::string_view {
 public:
  // These constructors recreate the std constuctor API.
  constexpr StringPiece(const StringPiece&) = default;
  constexpr StringPiece() : std::string_view() {}
  constexpr StringPiece(const char* s, size_t count) : std::string_view(s, count) {}
  constexpr StringPiece(const char* s) : std::string_view(s) {}

  // The remaining constructors are non-standard.

  // Creates a string piece from a string-like object.
  //
  // Works with various string types including fbl::String, fbl::StringView,
  // std::string, and std::string_view.
  template <typename T, typename = typename std::enable_if<is_string_like<T>::value>::type>
  constexpr StringPiece(const T& value)
      : StringPiece(GetStringData(value), GetStringLength(value)) {}

  StringPiece(decltype(nullptr)) = delete;
};

}  // namespace fbl

#endif  // FBL_STRING_PIECE_H_
