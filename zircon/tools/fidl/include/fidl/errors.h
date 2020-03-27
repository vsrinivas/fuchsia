// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_ERRORS_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_ERRORS_H_

#include <cassert>
#include <string_view>

namespace fidl {

// Forward decl
namespace flat {
  struct Constant;
  struct IdentifierConstant;
}

constexpr int count_format_args(std::string_view s, size_t i = 0) {
    if (i+1 >= s.size()) {
        return 0;
    }
    int extra = 0;
    if (s[i] == '{' && s[i+1] == '}') {
        extra = 1;
    }
    return extra + count_format_args(s, i+1);
}

template <typename ...Args>
struct Error {
  std::string_view msg;

  constexpr Error(std::string_view msg) : msg(msg) {
    assert(sizeof...(Args) == count_format_args(msg) &&
           "number of format string parameters '{}' != number of template arguments");
  }
};

constexpr Error<std::string> ErrDuplicateLibraryImport(
  "Library {} already imported. Did you require it twice?"
);
constexpr Error<> ErrOrOperatorOnNonPrimitiveValue(
  "Or operator can only be applied to primitive-kinded values"
);
constexpr Error<> ErrUnknownEnumMember(
  "Unknown enum member"
);
constexpr Error<> ErrUnknownBitsMember(
  "Unknown bits member"
);
constexpr Error<flat::IdentifierConstant *> ErrExpectedValueButGotType(
  "{} is a type, but a value was expected"
);
constexpr Error<std::string, std::string> ErrUnknownDependentLibrary(
  "Unknown dependent library {} or reference to member of "
                   "library {}. Did you require it with `using`?"
);

}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_ERRORS_H_
