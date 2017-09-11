// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_VMO_STRINGS_H_
#define LIB_MTL_VMO_STRINGS_H_

#include <mx/vmo.h>

#include <string>

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/strings/string_view.h"

namespace mtl {

// Make a new shared buffer with the contents of a string.
FXL_EXPORT bool VmoFromString(const fxl::StringView& string,
                              mx::vmo* handle_ptr);

// Copy the contents of a shared buffer into a string.
FXL_EXPORT bool StringFromVmo(const mx::vmo& handle, std::string* string_ptr);

}  // namespace mtl

#endif  // LIB_MTL_VMO_STRINGS_H_
