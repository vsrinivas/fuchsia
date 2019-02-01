// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Functions for converting between numbers and their representations as strings
// (in decimal, in a locale-independent way).

#ifndef LIB_FXL_STRINGS_STRING_NUMBER_CONVERSIONS_H_
#define LIB_FXL_STRINGS_STRING_NUMBER_CONVERSIONS_H_

#include <string>

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/strings/string_view.h"

namespace fxl {

// Supported base values used for converting a string to a number. Currently
// only bases 10 and 16 are supported.
enum class Base { k10, k16 };

// Converts |number| to a string with a locale-independent decimal
// representation of it. This is available for all |NumberType|s (u)intN_t (from
// <stdint.h>) and also (unsigned) int.
template <typename NumberType>
FXL_EXPORT std::string NumberToString(NumberType number, Base base = Base::k10);

// Converts |string| containing a locale-independent representation of a
// number to a numeric representation of that number. (On error, this returns
// false and leaves |*number| alone.) This is available for all |NumberType|s
// (u)intN_t (from <stdint.h>) and also (unsigned) int.
//
// Notes: Unary '+' is not allowed. Leading zeros are allowed (and ignored). For
// unsigned types, unary '-' is not allowed. For signed types, "-0", "-00", etc.
// are also allowed.
template <typename NumberType>
FXL_EXPORT bool StringToNumberWithError(fxl::StringView string,
                                        NumberType* number,
                                        Base base = Base::k10);

// Converts |string| containing a locale-independent representation of a
// number to a numeric representation of that number. (On error, this returns
// zero.) This is available for all |NumberType|s (u)intN_t (from <stdint.h>)
// and also (unsigned) int. (See |StringToNumberWithError()| for more details.)
template <typename NumberType>
NumberType StringToNumber(fxl::StringView string, Base base = Base::k10) {
  NumberType rv = static_cast<NumberType>(0);
  return StringToNumberWithError(string, &rv, base)
             ? rv
             : static_cast<NumberType>(0);
}

}  // namespace fxl

#endif  // LIB_FXL_STRINGS_STRING_NUMBER_CONVERSIONS_H_
