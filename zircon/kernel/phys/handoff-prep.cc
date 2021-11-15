// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "handoff-prep.h"

#include <lib/trivial-allocator/new.h>
#include <zircon/assert.h>

#include <phys/handoff.h>

void HandoffPrep::Init(ktl::span<ktl::byte> buffer) {
  // TODO(fxbug.dev/84107): Use the buffer inside the data ZBI via a
  // SingleHeapAllocator.  Later allocator() will return a real(ish) allocator.
  allocator_.allocate_function() = AllocateFunction(buffer);

  fbl::AllocChecker ac;
  handoff_ = new (allocator(), ac) PhysHandoff;
  ZX_ASSERT_MSG(ac.check(), "handoff buffer too small for PhysHandoff!");
}
