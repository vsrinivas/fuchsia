// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/lib/fxl/arraysize.h>

#include "garnet/bin/cpuperf_provider/arm64-events.h"
#include "garnet/bin/cpuperf_provider/categories.h"

namespace cpuperf_provider {

#define DEF_FIXED_CATEGORY(symbol, name, events...) \
  const perfmon::EventId symbol##_events[] = {events};
#define DEF_ARCH_CATEGORY(symbol, name, events...) \
  const perfmon::EventId symbol##_events[] = {events};
#include "arm64-pm-categories.inc"

extern const CategorySpec kTargetCategories[] = {

// Fixed events.
#define DEF_FIXED_CATEGORY(symbol, name, events...)                     \
  {"cpu:" name, CategoryGroup::kFixedArch, 0, arraysize(symbol##_events), \
   &symbol##_events[0]},
#include "arm64-pm-categories.inc"

// Architecturally specified programmable events.
#define DEF_ARCH_CATEGORY(symbol, name, events...)                             \
  {"cpu:" name, CategoryGroup::kProgrammableArch, 0, arraysize(symbol##_events), \
   &symbol##_events[0]},
#include "arm64-pm-categories.inc"

}; // kTargetCategories

extern const size_t kNumTargetCategories = arraysize(kTargetCategories);

extern const TimebaseSpec kTimebaseCategories[] = {
#define DEF_TIMEBASE_CATEGORY(symbol, name, event) {"cpu:" name, event},
#include "arm64-timebase-categories.inc"
};

extern const size_t kNumTimebaseCategories = arraysize(kTimebaseCategories);

}  // namespace cpuperf_provider
