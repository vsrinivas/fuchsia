// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cpuperf_provider/events.h"

#include <assert.h>
#include <zircon/device/intel-pm.h>
#include <unordered_map>

#include "lib/fxl/logging.h"

namespace cpuperf_provider {

const EventDetails g_arch_event_details[] = {
#define DEF_ARCH_EVENT(symbol, ebx_bit, event, umask, flags, name) \
  { event, umask, flags, name },
#include <zircon/device/intel-pm-events.inc>
};

const EventDetails g_skl_event_details[] = {
#define DEF_SKL_EVENT(symbol, event, umask, flags, name) \
  { event, umask, flags, name },
#include <zircon/device/intel-pm-events.inc>
};

using EventSelectMap = std::unordered_map<uint32_t, const EventDetails*>;

static EventSelectMap* g_arch_event_select_map;
static EventSelectMap* g_skl_event_select_map;

uint32_t MakeEventKey(const EventDetails& d) {
  FXL_DCHECK(d.event < (1 << IA32_PERFEVTSEL_EVENT_SELECT_LEN));
  FXL_DCHECK(d.umask < (1 << IA32_PERFEVTSEL_UMASK_LEN));
  FXL_DCHECK((d.flags & IPM_REG_FLAG_CMSK_MASK) < (1 << IA32_PERFEVTSEL_CMASK_LEN));
  uint32_t key = ((d.event << IA32_PERFEVTSEL_EVENT_SELECT_SHIFT) |
                  (d.umask << IA32_PERFEVTSEL_UMASK_SHIFT) |
                  ((d.flags & IPM_REG_FLAG_CMSK_MASK) << IA32_PERFEVTSEL_CMASK_SHIFT) |
                  (!!(d.flags & IPM_REG_FLAG_ANYT) << IA32_PERFEVTSEL_ANY_SHIFT));
  return key;
}

static void InitializeEventSelectMap(const EventDetails* details, size_t count,
                                     EventSelectMap* map) {
  for (size_t i = 0; i < count; ++i) {
    const EventDetails* d = &details[i];
    uint32_t key = MakeEventKey(*d);
    FXL_DCHECK(map->count(key) == 0);
    (*map)[key] = d;
  }
}

// Call this from main, before anything that needs to use
// |g_*_event_select_map|.
void InitializeEventSelectMaps() {
  g_arch_event_select_map = new EventSelectMap();
  InitializeEventSelectMap(g_arch_event_details, countof(g_arch_event_details),
                           g_arch_event_select_map);

  // TODO(dje): For now assume skylake/kabylake.
  g_skl_event_select_map = new EventSelectMap();
  InitializeEventSelectMap(g_skl_event_details, countof(g_skl_event_details),
                           g_skl_event_select_map);
}

bool EventSelectToEventDetails(uint64_t event_select,
                               const EventDetails** details) {
  uint32_t key = event_select & (IA32_PERFEVTSEL_EVENT_SELECT_MASK |
                                 IA32_PERFEVTSEL_UMASK_MASK |
                                 IA32_PERFEVTSEL_CMASK_MASK |
                                 IA32_PERFEVTSEL_ANY_MASK);
  if (g_arch_event_select_map->count(key)) {
    *details = (*g_arch_event_select_map)[key];
    return true;
  }

  if (g_skl_event_select_map->count(key)) {
    *details = (*g_skl_event_select_map)[key];
    return true;
  }

  return false;
}

const EventDetails* GetFixedEventDetails(int n) {
  enum {
#define DEF_ARCH_EVENT(symbol, ebx_bit, event, umask, flags, name) \
  EVENT_ ## symbol,
#include <zircon/device/intel-pm-events.inc>
  };

  switch (n) {
    case 0:
      return &g_arch_event_details[EVENT_ARCH_INSTRUCTIONS_RETIRED];
    case 1:
      return &g_arch_event_details[EVENT_ARCH_UNHALTED_CORE_CYCLES];
    case 2:
      return &g_arch_event_details[EVENT_ARCH_UNHALTED_REFERENCE_CYCLES];
    default:
      assert(0);
      __UNREACHABLE;
  }
}

}  // namespace cpuperf_provider
