// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_runtime/driver_context.h"

#include <zircon/assert.h>

#include <vector>

#include "src/devices/bin/driver_runtime/dispatcher.h"
#include "src/devices/lib/log/log.h"

namespace {

struct Entry {
  const void* driver;
  driver_runtime::Dispatcher* dispatcher;
};

static thread_local std::vector<Entry> g_driver_call_stack;
// The latest generation seen by this thread.
static thread_local uint32_t g_cached_irqs_generation = 0;

}  // namespace

namespace driver_context {

void PushDriver(const void* driver, driver_runtime::Dispatcher* dispatcher) {
  // TODO(fxbug.dev/88520): re-enable this once driver host v1 is deprecated.
  // ZX_DEBUG_ASSERT(IsDriverInCallStack(driver) == false);
  if (IsDriverInCallStack(driver)) {
    LOGF(TRACE, "DriverContext: tried to push driver %p that was already in stack\n", driver);
  }
  g_driver_call_stack.push_back({driver, dispatcher});
}

void PopDriver() {
  ZX_ASSERT(!g_driver_call_stack.empty());
  g_driver_call_stack.pop_back();
}

const void* GetCurrentDriver() {
  return g_driver_call_stack.empty() ? nullptr : g_driver_call_stack.back().driver;
}

driver_runtime::Dispatcher* GetCurrentDispatcher() {
  return g_driver_call_stack.empty() ? nullptr : g_driver_call_stack.back().dispatcher;
}

bool IsDriverInCallStack(const void* driver) {
  for (int64_t i = g_driver_call_stack.size() - 1; i >= 0; i--) {
    if (g_driver_call_stack[i].driver == driver) {
      return true;
    }
  }
  return false;
}

bool IsCallStackEmpty() { return g_driver_call_stack.empty(); }

void OnThreadWakeup(driver_runtime::DispatcherCoordinator& coordinator) {
  uint32_t new_irq_generation_id;
  coordinator.OnThreadWakeup(g_cached_irqs_generation, &new_irq_generation_id);
  g_cached_irqs_generation = new_irq_generation_id;
}

}  // namespace driver_context
