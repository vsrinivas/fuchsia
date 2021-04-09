// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_MEMORY_MONITOR_DEBUGGER_H_
#define SRC_DEVELOPER_MEMORY_MONITOR_DEBUGGER_H_

#include <fuchsia/memory/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include "src/developer/memory/monitor/pressure_notifier.h"

namespace monitor {

class MemoryDebugger : public fuchsia::memory::Debugger {
 public:
  MemoryDebugger(sys::ComponentContext *context, PressureNotifier *notifier);
  void SignalMemoryPressure(fuchsia::memorypressure::Level level) final;

 private:
  fidl::BindingSet<fuchsia::memory::Debugger> bindings_;
  PressureNotifier *const notifier_;
};

}  // namespace monitor

#endif  // SRC_DEVELOPER_MEMORY_MONITOR_DEBUGGER_H_
