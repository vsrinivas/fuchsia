// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915/scoped-value-change.h"

#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <set>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

namespace i915 {

namespace {

struct ChangeTracker {
  static ChangeTracker& Get() {
    static ChangeTracker singleton;
    return singleton;
  }
  fbl::Mutex mutex;
  std::set<void*> variables __TA_GUARDED(mutex);
};

}  // namespace

// static
void ScopedValueChange<void>::AddedChangeTo(void* variable) {
  ChangeTracker& change_tracker = ChangeTracker::Get();
  change_tracker.mutex.Acquire();
  auto [it, success] = change_tracker.variables.emplace(variable);
  change_tracker.mutex.Release();

  ZX_ASSERT_MSG(success, "Multiple ScopedValueChange instances created for the same variable");
}

// static
void ScopedValueChange<void>::RemovedChangeTo(void* variable) {
  ChangeTracker& change_tracker = ChangeTracker::Get();
  change_tracker.mutex.Acquire();
  const size_t erase_count = change_tracker.variables.erase(variable);
  change_tracker.mutex.Release();

  ZX_ASSERT_MSG(erase_count == 1, "Bug in ScopedValueChange lifecycle / reference counting");
}

}  // namespace i915
