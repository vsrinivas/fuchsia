// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains Intel events.
// When needed separate files will describe non-Intel x64 events.

#include <iterator>
#include <cpuid.h>

#include "src/performance/lib/perfmon/event-registry.h"
#include "src/performance/lib/perfmon/events.h"

namespace perfmon {

namespace {

enum class Microarch {
  kSkylake,
  kGoldmont,
  kUnknown
};

bool is_intel() {
  uint32_t a, b, c, d;
  __get_cpuid(0, &a, &b, &c, &d);
  return (b == signature_INTEL_ebx) &&
         (d == signature_INTEL_edx) &&
         (c == signature_INTEL_ecx);
}
Microarch microarch() {
  uint32_t a, b, c, d;
  __get_cpuid(1, &a, &b, &c, &d);
  uint32_t family = ((a >> 8) & 0xf) | ((a >> 16) & 0xff0);
  uint32_t model = ((a >> 4) & 0xf) | ((a >> 12) & 0xf0);

  if (family != 0x6) {
    return Microarch::kUnknown;
  }
  switch (model) {
  case 0x4E:  // Skylake-Y, -U
  case 0x5E:  // Skylake-DT, -H, -S
  case 0x8E:  // Kabylake-Y, -U; Whiskey Lake-U; Amber Lake-Y; Comet Lake-U
  case 0x9E:  // Kabylake-DT, -H, -S, -X; Coffee Lake-S, -H, -E; Comet Lake-S, -H
  case 0x55:  // Skylake-SP, Cascade Lake-SP
    return Microarch::kSkylake;
  case 0x5C:  // Apollo Lake
    return Microarch::kGoldmont;
  default:
    return Microarch::kUnknown;
  }
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wc99-designator"
const EventDetails g_fixed_event_details[] = {
#define DEF_FIXED_EVENT(symbol, event_name, id, regnum, flags, readable_name, description) \
  [id] = {MakeEventId(kGroupFixed, id), #event_name, readable_name, description},
#include <lib/zircon-internal/device/cpu-trace/intel-pm-events.inc>
};

const size_t g_num_fixed_event_details = std::size(g_fixed_event_details);

const EventDetails g_arch_event_details[] = {
#define DEF_ARCH_EVENT(symbol, event_name, id, ebx_bit, event, umask, flags, readable_name, \
                       description)                                                         \
  [id] = {MakeEventId(kGroupArch, id), #event_name, readable_name, description},
#include <lib/zircon-internal/device/cpu-trace/intel-pm-events.inc>
};

const size_t g_num_arch_event_details = std::size(g_arch_event_details);

const EventDetails g_skl_event_details[] = {
#define DEF_SKL_EVENT(symbol, event_name, id, event, umask, flags, readable_name, description) \
  [id] = {MakeEventId(kGroupModel, id), #event_name, readable_name, description},
#include <lib/zircon-internal/device/cpu-trace/skylake-pm-events.inc>
};

const size_t g_num_skl_event_details = std::size(g_skl_event_details);

const EventDetails g_skl_misc_event_details[] = {
#define DEF_MISC_SKL_EVENT(symbol, event_name, id, offset, size, flags, readable_name, \
                           description)                                                \
  [id] = {MakeEventId(kGroupMisc, id), #event_name, readable_name, description},
#include <lib/zircon-internal/device/cpu-trace/skylake-misc-events.inc>
};

const size_t g_num_skl_misc_event_details = std::size(g_skl_misc_event_details);

const EventDetails g_glm_event_details[] = {
#define DEF_GLM_EVENT(symbol, event_name, id, event, umask, flags, readable_name, description) \
  [id] = {MakeEventId(kGroupModel, id), #event_name, readable_name, description},
#include <lib/zircon-internal/device/cpu-trace/goldmont-pm-events.inc>
};

const size_t g_num_glm_event_details = std::size(g_glm_event_details);
#pragma GCC diagnostic pop

// Register all events for Intel Skylake.
void RegisterIntelSkylakeEvents(internal::EventRegistry* registry) {
  // TODO(dje): Clear table first (start over).
  registry->RegisterEvents("skylake", "fixed", g_fixed_event_details, g_num_fixed_event_details);
  registry->RegisterEvents("skylake", "arch", g_arch_event_details, g_num_arch_event_details);
  registry->RegisterEvents("skylake", "model", g_skl_event_details, g_num_skl_event_details);
  registry->RegisterEvents("skylake", "misc", g_skl_misc_event_details,
                           g_num_skl_misc_event_details);
}

void RegisterIntelGoldmontEvents(internal::EventRegistry* registry) {
  registry->RegisterEvents("goldmont", "fixed", g_fixed_event_details, g_num_fixed_event_details);
  registry->RegisterEvents("goldmont", "arch", g_arch_event_details, g_num_arch_event_details);
  registry->RegisterEvents("goldmont", "model", g_glm_event_details, g_num_glm_event_details);
}

}  // namespace

namespace internal {

void RegisterAllIntelModelEvents(internal::EventRegistry* registry) {
  if (!is_intel()) {
    return;
  }

  switch (microarch()) {
  case Microarch::kSkylake:
    RegisterIntelSkylakeEvents(registry);
    break;
  case Microarch::kGoldmont:
    RegisterIntelGoldmontEvents(registry);
    break;
  case Microarch::kUnknown:
    break;
  }
}

}  // namespace internal

}  // namespace perfmon
