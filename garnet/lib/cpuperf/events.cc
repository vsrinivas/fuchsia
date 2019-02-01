// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>

#include <lib/fxl/arraysize.h>
#include <lib/fxl/logging.h>

#include "garnet/lib/cpuperf/events.h"

namespace cpuperf {

const EventDetails g_arch_event_details[] = {
#define DEF_ARCH_EVENT(symbol, event_name, id, ebx_bit, event, \
                       umask, flags, readable_name, description) \
  [id] = {CPUPERF_MAKE_EVENT_ID(CPUPERF_GROUP_ARCH, id), #event_name, \
          readable_name, description},
#include <lib/zircon-internal/device/cpu-trace/intel-pm-events.inc>
};

const EventDetails g_fixed_event_details[] = {
#define DEF_FIXED_EVENT(symbol, event_name, id, regnum, flags, \
                        readable_name, description) \
  [id] = {CPUPERF_MAKE_EVENT_ID(CPUPERF_GROUP_FIXED, id), #event_name, \
          readable_name, description},
#include <lib/zircon-internal/device/cpu-trace/intel-pm-events.inc>
};

const EventDetails g_skl_event_details[] = {
#define DEF_SKL_EVENT(symbol, event_name, id, event, umask, \
                      flags, readable_name, description) \
  [id] = {CPUPERF_MAKE_EVENT_ID(CPUPERF_GROUP_MODEL, id), #event_name, \
          readable_name, description},
#include <lib/zircon-internal/device/cpu-trace/skylake-pm-events.inc>
};

const EventDetails g_skl_misc_event_details[] = {
#define DEF_MISC_SKL_EVENT(symbol, event_name, id, offset, size, \
                           flags, readable_name, description) \
  [id] = {CPUPERF_MAKE_EVENT_ID(CPUPERF_GROUP_MISC, id), #event_name, \
          readable_name, description},
#include <lib/zircon-internal/device/cpu-trace/skylake-misc-events.inc>
};

bool EventIdToEventDetails(cpuperf_event_id_t id,
                           const EventDetails** out_details) {
  unsigned event = CPUPERF_EVENT_ID_EVENT(id);
  const EventDetails* details;

  switch (CPUPERF_EVENT_ID_GROUP(id)) {
    case CPUPERF_GROUP_ARCH:
      details = &g_arch_event_details[event];
      break;
    case CPUPERF_GROUP_FIXED:
      details = &g_fixed_event_details[event];
      break;
    case CPUPERF_GROUP_MODEL:
      // TODO(dje): For now assume Skylake, Kaby Lake.
      details = &g_skl_event_details[event];
      break;
    case CPUPERF_GROUP_MISC:
      // TODO(dje): For now assume Skylake, Kaby Lake.
      details = &g_skl_misc_event_details[event];
      break;
    default:
      return false;
  }

  if (details->id == 0)
    return false;
  *out_details = details;
  return true;
}

// This just uses a linear search for now.
bool LookupEventByName(const char* group_name, const char* event_name,
                       const EventDetails** out_details) {
  const EventDetails* details;
  size_t num_events;

  if (strcmp(group_name, "arch") == 0) {
    details = &g_arch_event_details[0];
    num_events = arraysize(g_arch_event_details);
  } else if (strcmp(group_name, "fixed") == 0) {
    details = &g_fixed_event_details[0];
    num_events = arraysize(g_fixed_event_details);
  } else if (strcmp(group_name, "model") == 0) {
    details = &g_skl_event_details[0];
    num_events = arraysize(g_skl_event_details);
  } else if (strcmp(group_name, "misc") == 0) {
    details = &g_skl_misc_event_details[0];
    num_events = arraysize(g_skl_misc_event_details);
  } else {
    return false;
  }

  for (size_t i = 0; i < num_events; ++i) {
    if (details[i].id == 0)
      continue;
    if (strcmp(details[i].name, event_name) == 0) {
      *out_details = &details[i];
      return true;
    }
  }

  return false;
}

size_t GetConfigEventCount(const cpuperf_config_t& config) {
  size_t count;
  for (count = 0; count < CPUPERF_MAX_EVENTS; ++count) {
    if (config.events[count] == CPUPERF_EVENT_ID_NONE)
      break;
  }
  return count;
}

static void AddEvents(GroupTable& gt, const char* group_name_literal,
                      const EventDetails* details, size_t count) {
  gt.emplace_back(GroupEvents{group_name_literal, {}});
  auto& v = gt.back();
  for (size_t i = 0; i < count; ++i) {
    if (details[i].id != CPUPERF_EVENT_ID_NONE) {
      v.events.push_back(&details[i]);
    }
  }
}

GroupTable GetAllGroups() {
  GroupTable groups;

  AddEvents(groups, "arch", &g_arch_event_details[0],
            arraysize(g_arch_event_details));
  AddEvents(groups, "fixed", &g_fixed_event_details[0],
            arraysize(g_fixed_event_details));
  AddEvents(groups, "model", &g_skl_event_details[0],
            arraysize(g_skl_event_details));
  AddEvents(groups, "misc", &g_skl_misc_event_details[0],
            arraysize(g_skl_misc_event_details));

  return groups;
}

}  // namespace cpuperf
