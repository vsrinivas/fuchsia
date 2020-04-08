// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_STRING_VIEW_H_
#define ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_STRING_VIEW_H_

#include <string_view>

namespace ktl {

using std::basic_string_view;
using std::string_view;

}  // namespace ktl

// Just including <ktl/string_view.h> enables "foo"sv literals.
using namespace std::literals::string_view_literals;

#endif  // ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_STRING_VIEW_H_
