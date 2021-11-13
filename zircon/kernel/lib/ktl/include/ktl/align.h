// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_ALIGN_H_
#define ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_ALIGN_H_

#include <memory>

namespace ktl {

// This is not really likely to be used in kernel code.  But it's used by some
// kernel-compatible library code and libc++'s header implementation fails to
// inline it for some reason, so we need to provide a definition.
using std::align;

}  // namespace ktl

#endif  // ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_ALIGN_H_
