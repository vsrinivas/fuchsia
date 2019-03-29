// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains the details of what models and their events are
// registered. For host builds we want everything. For target builds we
// only want that target.

#include <src/lib/fxl/logging.h>

#include "garnet/lib/perfmon/event-registry.h"
#include "garnet/lib/perfmon/events.h"

namespace perfmon {
namespace internal {

void EventRegistry::RegisterEvents(const char* model_name,
                                   const char* group_name,
                                   const EventDetails* events, size_t count) {
  FXL_VLOG(1) << "Registering " << model_name << " " << group_name
              << " events";

  ModelEvents& model_events = (*this)[model_name];
  ModelEventManager::EventTable* table = nullptr;

  if (strcmp(group_name, kArchGroupName) == 0) {
    table = &model_events.arch_events;
  } else if (strcmp(group_name, kFixedGroupName) == 0) {
    table = &model_events.fixed_events;
  } else if (strcmp(group_name, kModelGroupName) == 0) {
    table = &model_events.model_events;
  } else if (strcmp(group_name, kMiscGroupName) == 0) {
    table = &model_events.misc_events;
  }
  FXL_CHECK(table != nullptr);

  table->reserve(table->size() + count);

  for (size_t i = 0; i < count; ++i) {
    table->push_back(&events[i]);
  }
}

void RegisterCurrentArchEvents(EventRegistry* registry) {
#ifdef __x86_64__
  RegisterAllIntelModelEvents(registry);
#elif defined(__aarch64__)
  RegisterAllArm64ModelEvents(registry);
#endif
}

}  // namespace internal
}  // namespace perfmon
