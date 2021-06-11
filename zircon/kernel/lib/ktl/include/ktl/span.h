// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_SPAN_H_
#define ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_SPAN_H_

#include <lib/stdcompat/span.h>

#include "type_traits.h"

namespace ktl {

using cpp20::span;

using cpp20::as_bytes;
using cpp20::as_writable_bytes;

using cpp20::dynamic_extent;

using std::size;

}  // namespace ktl

#endif  // ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_SPAN_H_
