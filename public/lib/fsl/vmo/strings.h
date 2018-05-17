// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FSL_VMO_STRINGS_H_
#define LIB_FSL_VMO_STRINGS_H_

#include <string>

#include <fuchsia/mem/cpp/fidl.h>

#include <lib/zx/vmo.h>

#include "lib/fsl/vmo/sized_vmo.h"
#include "lib/fxl/fxl_export.h"
#include "lib/fxl/strings/string_view.h"

namespace fsl {

// Make a new shared buffer with the contents of a string.
FXL_EXPORT bool VmoFromString(const fxl::StringView& string,
                              SizedVmo* handle_ptr);

// Copy the contents of a shared buffer into a string.
FXL_EXPORT bool StringFromVmo(const SizedVmo& handle, std::string* string_ptr);

// Copy the contents of a shared buffer into a string.
FXL_EXPORT bool StringFromVmo(const fuchsia::mem::Buffer& handle,
                              std::string* string_ptr);

}  // namespace fsl

#endif  // LIB_FSL_VMO_STRINGS_H_
