// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_STRING_VIEW_H_
#define ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_STRING_VIEW_H_

#include <lib/stdcompat/string_view.h>

namespace ktl {

using std::basic_string_view;
using std::string_view;

using cpp20::ends_with;
using cpp20::starts_with;

}  // namespace ktl

// Just including <ktl/string_view.h> enables "foo"sv literals.
using namespace std::literals::string_view_literals;

#endif  // ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_STRING_VIEW_H_
