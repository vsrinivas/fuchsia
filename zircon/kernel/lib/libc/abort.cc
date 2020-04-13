// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdlib.h>
#include <zircon/assert.h>

void abort() { ZX_PANIC("abort() called!\n"); }

// The compiler generates calls to this for -fstack-protector.
extern "C" void __stack_chk_fail() { ZX_PANIC("stack canary corrupted!\n"); }
