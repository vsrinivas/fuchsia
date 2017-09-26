// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CPUPERF_PROVIDER_EVENTS_H_
#define GARNET_BIN_CPUPERF_PROVIDER_EVENTS_H_

#include <stdint.h>

namespace cpuperf_provider {

struct EventDetails {
  uint32_t event;
  uint32_t umask;
  uint32_t flags;
  const char* name;
};

void InitializeEventSelectMaps();

uint32_t MakeEventKey(const EventDetails& d);

// Given an IA32_PERFEVTSEL MSR value, return its event kind in |*details| and
// return true, or return false if the event is unknown.
bool EventSelectToEventDetails(uint64_t event_select,
                               const EventDetails** details);

// Return the details for fixed event |n|.
// |n| must be between 0 and $num_fixed_counters - 1.
const EventDetails* GetFixedEventDetails(int n);

}  // namespace cpuperf_provider

#endif  // GARNET_BIN_CPUPERF_PROVIDER_EVENTS_H_
