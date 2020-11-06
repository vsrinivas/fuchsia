// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google, Inc. All rights reserved
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_DEV_HW_RNG_INCLUDE_DEV_HW_RNG_H_
#define ZIRCON_KERNEL_DEV_HW_RNG_INCLUDE_DEV_HW_RNG_H_

#include <assert.h>
#include <debug.h>
#include <sys/types.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// Hardware RNG interface.
struct hw_rng_ops {
  size_t (*hw_rng_get_entropy)(void* buf, size_t len);
};

// Draw entropy from hardware RNG. 
//
// The caller is responsible to check that the return value equals len. 
// Otherwise it means the operation failed.
__WARN_UNUSED_RESULT size_t hw_rng_get_entropy(void* buf, size_t len);

// Register the ops of hardware RNG with HW RNG driver.
void hw_rng_register(const struct hw_rng_ops* ops);

// Return whether there is a functioning hardware RNG.
bool hw_rng_is_registered();

__END_CDECLS

#endif  // ZIRCON_KERNEL_DEV_HW_RNG_INCLUDE_DEV_HW_RNG_H_
