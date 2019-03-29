// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains Intel events.
// When needed separate files will describe non-Intel x64 events.

#include <src/lib/fxl/arraysize.h>

#include "garnet/lib/perfmon/event-registry.h"
#include "garnet/lib/perfmon/events.h"

namespace perfmon {

namespace {

const EventDetails g_fixed_event_details[] = {
#define DEF_FIXED_EVENT(symbol, event_name, id, regnum, flags, \
                        readable_name, description) \
  [id] = {PERFMON_MAKE_EVENT_ID(PERFMON_GROUP_FIXED, id), #event_name, \
          readable_name, description},
#include <lib/zircon-internal/device/cpu-trace/intel-pm-events.inc>
};

const size_t g_num_fixed_event_details = arraysize(g_fixed_event_details);

const EventDetails g_arch_event_details[] = {
#define DEF_ARCH_EVENT(symbol, event_name, id, ebx_bit, event, \
                       umask, flags, readable_name, description) \
  [id] = {PERFMON_MAKE_EVENT_ID(PERFMON_GROUP_ARCH, id), #event_name, \
          readable_name, description},
#include <lib/zircon-internal/device/cpu-trace/intel-pm-events.inc>
};

const size_t g_num_arch_event_details = arraysize(g_arch_event_details);

const EventDetails g_skl_event_details[] = {
#define DEF_SKL_EVENT(symbol, event_name, id, event, umask, \
                      flags, readable_name, description) \
  [id] = {PERFMON_MAKE_EVENT_ID(PERFMON_GROUP_MODEL, id), #event_name, \
          readable_name, description},
#include <lib/zircon-internal/device/cpu-trace/skylake-pm-events.inc>
};

const size_t g_num_skl_event_details = arraysize(g_skl_event_details);

const EventDetails g_skl_misc_event_details[] = {
#define DEF_MISC_SKL_EVENT(symbol, event_name, id, offset, size, \
                           flags, readable_name, description) \
  [id] = {PERFMON_MAKE_EVENT_ID(PERFMON_GROUP_MISC, id), #event_name, \
          readable_name, description},
#include <lib/zircon-internal/device/cpu-trace/skylake-misc-events.inc>
};

const size_t g_num_skl_misc_event_details =
  arraysize(g_skl_misc_event_details);

// Register all events for Intel Skylake.
void RegisterIntelSkylakeEvents(internal::EventRegistry* registry) {
  // TODO(dje): Clear table first (start over).
  registry->RegisterEvents("skylake", "fixed", g_fixed_event_details,
                           g_num_fixed_event_details);
  registry->RegisterEvents("skylake", "arch", g_arch_event_details,
                           g_num_arch_event_details);
  registry->RegisterEvents("skylake", "model", g_skl_event_details,
                           g_num_skl_event_details);
  registry->RegisterEvents("skylake", "misc", g_skl_misc_event_details,
                           g_num_skl_misc_event_details);
}

}  // namespace anonymous

namespace internal {

void RegisterAllIntelModelEvents(internal::EventRegistry* registry) {
  RegisterIntelSkylakeEvents(registry);
}

}  // namespace internal

}  // namespace perfmon
