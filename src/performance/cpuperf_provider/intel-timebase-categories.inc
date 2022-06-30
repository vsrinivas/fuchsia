// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Table of categories for specifying a timebase.
// A "timebase" is an event that is used to drive the collection of data
// for other events. This is useful when one wishes to collect a set of events
// when one event triggers. This one event is called the "timebase".

#ifndef DEF_TIMEBASE_CATEGORY
#define DEF_TIMEBASE_CATEGORY(symbol, name, events)
#endif

// Categories for timebase events
// Arguments:
// - symbol (must be unique among all categories)
// - name (must be unique among all categories,
//         cannot have any spaces: used in trace category name)
// - event id

DEF_TIMEBASE_CATEGORY(IPM_CATEGORY_TIMEBASE_FIXED_INSTRUCTIONS_RETIRED,
  "timebase:fixed:instructions_retired",
  FIXED_INSTRUCTIONS_RETIRED)

DEF_TIMEBASE_CATEGORY(IPM_CATEGORY_TIMEBASE_FIXED_UNHALTED_REFERENCE_CYCLES,
  "timebase:fixed:unhalted_reference_cycles",
  FIXED_UNHALTED_REFERENCE_CYCLES)

#undef DEF_TIMEBASE_CATEGORY
