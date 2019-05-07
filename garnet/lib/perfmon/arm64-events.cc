// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains arm64 events.
// When needed separate files will describe various models.

#include <src/lib/fxl/arraysize.h>

#include "garnet/lib/perfmon/event-registry.h"
#include "garnet/lib/perfmon/events.h"

namespace perfmon {

namespace {

const EventDetails g_fixed_event_details[] = {
#define DEF_FIXED_EVENT(symbol, event_name, id, regnum, flags, \
                        readable_name, description) \
  [id] = {MakeEventId(kGroupFixed, id), #event_name, \
          readable_name, description},
#include <lib/zircon-internal/device/cpu-trace/arm64-pm-events.inc>
};

const size_t g_num_fixed_event_details = arraysize(g_fixed_event_details);

const EventDetails g_arch_event_details[] = {
#define DEF_ARCH_EVENT(symbol, event_name, id, pmceid_bit, event, flags, \
                       readable_name, description) \
  [id] = {MakeEventId(kGroupArch, id), #event_name, \
          readable_name, description},
#include <lib/zircon-internal/device/cpu-trace/arm64-pm-events.inc>
};

const size_t g_num_arch_event_details = arraysize(g_arch_event_details);

// Register all events for armv8.
void RegisterArmv8Events(internal::EventRegistry* registry) {
  // TODO(dje): Clear table first (start over).
  registry->RegisterEvents("armv8", "fixed", g_fixed_event_details,
                           g_num_fixed_event_details);
  registry->RegisterEvents("armv8", "arch", g_arch_event_details,
                           g_num_arch_event_details);
}

}  // namespace anonymous

namespace internal {

void RegisterAllArm64ModelEvents(EventRegistry* registry) {
  RegisterArmv8Events(registry);
}

}  // namespace internal

}  // namespace perfmon
