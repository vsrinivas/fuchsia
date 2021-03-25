// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/operation/helpers/alloc_checker.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <cstdlib>
#include <new>

namespace operation {

void AllocChecker::CheckNotCalledPanic() { ZX_PANIC("check() not called on AllocChecker."); }

void AllocChecker::ArmedTwicePanic() { ZX_PANIC("arm() called twice on AllocChecker."); }

}  // namespace operation
