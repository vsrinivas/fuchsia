// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_CPUPERF_EVENTS_H_
#define GARNET_LIB_CPUPERF_EVENTS_H_

#include <stdint.h>

#include <lib/zircon-internal/device/cpu-trace/cpu-perf.h>

namespace cpuperf {

// TODO(dje): Reconcile event SYMBOLs with event names.
// Ideally they should match, but there's also good reasons
// to keep them different (organization, and matching vendor docs).
// TODO(dje): Add missing event descriptions. See cpuperf --list-events.

struct EventDetails {
  cpuperf_event_id_t id;
  const char* name;
  const char* readable_name;
  const char* description;
};

// Look up the event details for event |id|.
// Returns true if |id| is valid, otherwise false.
// This function is thread-safe.
bool EventIdToEventDetails(cpuperf_event_id_t id,
                           const EventDetails** out_details);

}  // namespace cpuperf

#endif  // GARNET_LIB_CPUPERF_EVENTS_H_
