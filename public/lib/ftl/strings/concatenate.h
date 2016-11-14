// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_STRINGS_CONCATENATE_H_
#define LIB_FTL_STRINGS_CONCATENATE_H_

#include <initializer_list>
#include <string>

#include "lib/ftl/strings/string_view.h"

namespace ftl {

// Concatenates a fixed list of strings.
std::string Concatenate(std::initializer_list<ftl::StringView> string_views);

}  // namespace ftl

#endif  // LIB_FTL_STRINGS_CONCATENATE_H_
