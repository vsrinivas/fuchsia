// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_CPUPERF_EVENTS_H_
#define GARNET_LIB_CPUPERF_EVENTS_H_

#include <stdint.h>

#include <zircon/device/cpu-trace/cpu-perf.h>

namespace cpuperf {

struct EventDetails {
  cpuperf_event_id_t id;
  const char* name;
  const char* description;
};

// Look up the event details for event |id|.
// Returns true if |id| is valid, otherwise false.
// This function is thread-safe.
bool EventIdToEventDetails(cpuperf_event_id_t id,
                           const EventDetails** out_details);

// Return the event id for fixed counter |ctr|.
cpuperf_event_id_t GetFixedCounterId(unsigned ctr);

}  // namespace cpuperf

#endif  // GARNET_LIB_CPUPERF_EVENTS_H_
