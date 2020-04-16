// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_ALIGN_H_
#define ZIRCON_KERNEL_INCLUDE_ALIGN_H_

#include <arch/defines.h>

#define ROUNDUP(a, b) (((a) + ((b)-1)) & ~((b)-1))
#define ROUNDDOWN(a, b) ((a) & ~((b)-1))

#define ALIGN(a, b) ROUNDUP(a, b)
#define IS_ALIGNED(a, b) (!(((uintptr_t)(a)) & (((uintptr_t)(b)) - 1)))

#define PAGE_ALIGN(x) ALIGN((x), PAGE_SIZE)
#define ROUNDUP_PAGE_SIZE(x) ROUNDUP((x), PAGE_SIZE)
#define IS_PAGE_ALIGNED(x) IS_ALIGNED((x), PAGE_SIZE)

#endif  // ZIRCON_KERNEL_INCLUDE_ALIGN_H_
