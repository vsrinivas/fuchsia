// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbl/alloc_checker.h"

#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <cstdlib>
#include <new>

#if _KERNEL
#include <lib/heap.h>
#endif

namespace fbl {

void AllocChecker::CheckNotCalledPanic() { ZX_PANIC("check() not called on AllocChecker."); }

void AllocChecker::ArmedTwicePanic() { ZX_PANIC("arm() called twice on AllocChecker."); }

#if _KERNEL
// Ensure that the malloc default alignment is at least what C++'s new expects.
static_assert(HEAP_DEFAULT_ALIGNMENT >= __STDCPP_DEFAULT_NEW_ALIGNMENT__);
#endif

}  // namespace fbl
