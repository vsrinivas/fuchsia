// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2012 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_TARGET_H_
#define ZIRCON_KERNEL_INCLUDE_TARGET_H_

#include <stdbool.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

/* super early platform initialization, before almost everything */
void target_early_init(void);

/* later init, after the kernel has come up */
void target_init(void);

/* called during chain loading to make sure target specific bits are put into a stopped state */
void target_quiesce(void);

__END_CDECLS

#endif  // ZIRCON_KERNEL_INCLUDE_TARGET_H_
