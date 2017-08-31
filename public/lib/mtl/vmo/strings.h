// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_VMO_STRINGS_H_
#define LIB_MTL_VMO_STRINGS_H_

#include <mx/vmo.h>

#include <string>

#include "lib/ftl/ftl_export.h"
#include "lib/ftl/strings/string_view.h"

namespace mtl {

// Make a new shared buffer with the contents of a string.
FTL_EXPORT bool VmoFromString(const ftl::StringView& string,
                              mx::vmo* handle_ptr);

// Copy the contents of a shared buffer into a string.
FTL_EXPORT bool StringFromVmo(const mx::vmo& handle, std::string* string_ptr);

}  // namespace mtl

#endif  // LIB_MTL_VMO_STRINGS_H_
