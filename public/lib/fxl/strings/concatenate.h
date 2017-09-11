// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_STRINGS_CONCATENATE_H_
#define LIB_FXL_STRINGS_CONCATENATE_H_

#include <initializer_list>
#include <string>

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/strings/string_view.h"

namespace fxl {

// Concatenates a fixed list of strings.
FXL_EXPORT std::string Concatenate(
    std::initializer_list<fxl::StringView> string_views);

}  // namespace fxl

#endif  // LIB_FXL_STRINGS_CONCATENATE_H_
