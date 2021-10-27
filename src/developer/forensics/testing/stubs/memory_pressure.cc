// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/testing/stubs/memory_pressure.h"

#include <lib/fit/defer.h>

#include <memory>

namespace forensics::stubs {

MemoryPressure::MemoryPressure(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

void MemoryPressure::RegisterWatcher(
    ::fidl::InterfaceHandle<fuchsia::memorypressure::Watcher> watcher) {
  FX_CHECK(!watcher_.is_bound());
  watcher_.Bind(std::move(watcher), dispatcher_);
}

void MemoryPressure::ChangePressureLevel(fuchsia::memorypressure::Level level) {
  // Check-fail unless the callback is executed before destruction of the stub.
  auto check_unless_called = ::fit::defer([] { FX_CHECK(true); });
  watcher_->OnLevelChanged(level, [check_unless_called = std::move(check_unless_called)]() mutable {
    check_unless_called.cancel();
  });
}

}  // namespace forensics::stubs
