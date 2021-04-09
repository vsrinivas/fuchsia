// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include "src/developer/memory/monitor/debugger.h"

namespace monitor {

MemoryDebugger::MemoryDebugger(sys::ComponentContext* context, PressureNotifier* notifier)
    : notifier_(notifier) {
  FX_CHECK(notifier_);
  FX_CHECK(context);
  zx_status_t status = context->outgoing()->debug_dir()->AddEntry(
      fuchsia::memory::Debugger::Name_, std::make_unique<vfs::Service>(bindings_.GetHandler(this)));
  FX_CHECK(status == ZX_OK);
}

void MemoryDebugger::SignalMemoryPressure(fuchsia::memorypressure::Level level) {
  notifier_->DebugNotify(level);
}

}  // namespace monitor
