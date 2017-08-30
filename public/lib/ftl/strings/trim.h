// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_STRINGS_TRIM_H_
#define LIB_FTL_STRINGS_TRIM_H_

#include <string>

#include "lib/ftl/ftl_export.h"
#include "lib/ftl/strings/string_view.h"

namespace ftl {

// Returns a StringView over str, where chars_to_trim are removed from the
// beginning and end of the StringView.
FTL_EXPORT ftl::StringView TrimString(ftl::StringView str,
                                      ftl::StringView chars_to_trim);

}  // namespace ftl

#endif  // LIB_FTL_STRINGS_TRIM_H_
