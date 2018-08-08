// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/cpuperf/events.h"

#include <assert.h>

#include "lib/fxl/logging.h"

namespace cpuperf {

const EventDetails g_arch_event_details[] = {
#define DEF_ARCH_EVENT(symbol, id, ebx_bit, event, umask, flags, name, \
                       description)                                    \
  [id] = {CPUPERF_MAKE_EVENT_ID(CPUPERF_UNIT_ARCH, id), name, description},
#include <zircon/device/cpu-trace/intel-pm-events.inc>
};

const EventDetails g_fixed_event_details[] = {
#define DEF_FIXED_EVENT(symbol, id, regnum, flags, name, description) \
  [id] = {CPUPERF_MAKE_EVENT_ID(CPUPERF_UNIT_FIXED, id), name, description},
#include <zircon/device/cpu-trace/intel-pm-events.inc>
};

const EventDetails g_skl_event_details[] = {
#define DEF_SKL_EVENT(symbol, id, event, umask, flags, name, description) \
  [id] = {CPUPERF_MAKE_EVENT_ID(CPUPERF_UNIT_MODEL, id), name, description},
#include <zircon/device/cpu-trace/skylake-pm-events.inc>
};

const EventDetails g_misc_event_details[] = {
#define DEF_MISC_SKL_EVENT(symbol, id, offset, size, flags, name, description) \
  [id] = {CPUPERF_MAKE_EVENT_ID(CPUPERF_UNIT_MISC, id), name, description},
#include <zircon/device/cpu-trace/skylake-misc-events.inc>
};

bool EventIdToEventDetails(cpuperf_event_id_t id,
                           const EventDetails** out_details) {
  unsigned event = CPUPERF_EVENT_ID_EVENT(id);
  const EventDetails* details;

  switch (CPUPERF_EVENT_ID_UNIT(id)) {
    case CPUPERF_UNIT_ARCH:
      details = &g_arch_event_details[event];
      break;
    case CPUPERF_UNIT_FIXED:
      details = &g_fixed_event_details[event];
      break;
    case CPUPERF_UNIT_MODEL:
      // TODO(dje): For now assume Skylake, Kaby Lake.
      details = &g_skl_event_details[event];
      break;
    case CPUPERF_UNIT_MISC:
      // TODO(dje): For now assume Skylake, Kaby Lake.
      details = &g_misc_event_details[event];
      break;
    default:
      return false;
  }

  if (details->id == 0)
    return false;
  *out_details = details;
  return true;
}

}  // namespace cpuperf
