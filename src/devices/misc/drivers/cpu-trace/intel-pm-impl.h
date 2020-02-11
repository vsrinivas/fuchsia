// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

namespace perfmon {

// There's only a few fixed events, so handle them directly.
enum FixedEventId {
#define DEF_FIXED_EVENT(symbol, event_name, id, regnum, flags, readable_name, description) \
  symbol##_ID = MakeEventId(kGroupFixed, id),
#include <lib/zircon-internal/device/cpu-trace/intel-pm-events.inc>
};

// Verify each fixed counter regnum < IPM_MAX_FIXED_COUNTERS.
#define DEF_FIXED_EVENT(symbol, event_name, id, regnum, flags, readable_name, description) \
  &&(regnum) < IPM_MAX_FIXED_COUNTERS
static_assert(1
#include <lib/zircon-internal/device/cpu-trace/intel-pm-events.inc>
              ,
              "");

enum MiscEventId {
#define DEF_MISC_SKL_EVENT(symbol, event_name, id, offset, size, flags, readable_name, \
                           description)                                                \
  symbol##_ID = MakeEventId(kGroupMisc, id),
#include <lib/zircon-internal/device/cpu-trace/skylake-misc-events.inc>
};

// Misc event ids needn't be consecutive.
// Build a lookup table we can use to track duplicates.
enum MiscEventNumber {
#define DEF_MISC_SKL_EVENT(symbol, event_name, id, offset, size, flags, readable_name, \
                           description)                                                \
  symbol##_NUMBER,
#include <lib/zircon-internal/device/cpu-trace/skylake-misc-events.inc>
  IPM_NUM_MISC_EVENTS
};

struct StagingState {
  // Maximum number of each event we can handle.
  unsigned max_num_fixed;
  unsigned max_num_programmable;
  unsigned max_num_misc;

  // The number of events in use.
  unsigned num_fixed;
  unsigned num_programmable;
  unsigned num_misc;

  // The maximum value the counter can have before overflowing.
  uint64_t max_fixed_value;
  uint64_t max_programmable_value;

  // For catching duplicates of the fixed counters.
  bool have_fixed[IPM_MAX_FIXED_COUNTERS];
  // For catching duplicates of the misc events, 1 bit per event.
  uint64_t have_misc[(IPM_NUM_MISC_EVENTS + 63) / 64];
};

}  // namespace perfmon
