// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_CPUPERF_EVENTS_H_
#define GARNET_LIB_CPUPERF_EVENTS_H_

#include <stdint.h>

#include <vector>

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

struct GroupEvents {
  const char* group_name;
  std::vector<const EventDetails*> events;
};

using GroupTable = std::vector<GroupEvents>;

// Look up the event details for event |id|.
// Returns true if |id| is valid, otherwise false.
// This function is thread-safe.
bool EventIdToEventDetails(cpuperf_event_id_t id,
                           const EventDetails** out_details);

// Look up the event details for event |event_name| in group |group_name|.
// Returns true if |id| is valid, otherwise false.
// This function is thread-safe.
bool LookupEventByName(const char* group_name, const char* event_name,
                       const EventDetails** out_details);

// Return the number of events in |config|.
// This function is thread-safe.
size_t GetConfigEventCount(const cpuperf_config_t& config);

// Return set of all supported events.
// The result is an unsorted vector of vectors, one vector of events per group.
// This function is thread-safe.
GroupTable GetAllGroups();

}  // namespace cpuperf

#endif  // GARNET_LIB_CPUPERF_EVENTS_H_
