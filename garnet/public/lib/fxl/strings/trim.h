// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_STRINGS_TRIM_H_
#define LIB_FXL_STRINGS_TRIM_H_

#include <string>

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/strings/string_view.h"

namespace fxl {

// Returns a StringView over str, where chars_to_trim are removed from the
// beginning and end of the StringView.
FXL_EXPORT fxl::StringView TrimString(fxl::StringView str,
                                      fxl::StringView chars_to_trim);

}  // namespace fxl

#endif  // LIB_FXL_STRINGS_TRIM_H_
