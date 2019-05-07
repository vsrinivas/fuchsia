// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/lib/fxl/arraysize.h>

#include "garnet/bin/cpuperf_provider/categories.h"
#include "garnet/bin/cpuperf_provider/intel-events.h"

namespace cpuperf_provider {

// TODO(dje): Reorganize fixed,arch,skl(model),misc vs
// fixed/programmable+arch/model.

#define DEF_FIXED_CATEGORY(symbol, name, events...) \
  const perfmon::EventId symbol##_events[] = {events};
#define DEF_ARCH_CATEGORY(symbol, name, events...) \
  const perfmon::EventId symbol##_events[] = {events};
#include "intel-pm-categories.inc"

#define DEF_SKL_CATEGORY(symbol, name, events...) \
  const perfmon::EventId symbol##_events[] = {events};
#include "skylake-pm-categories.inc"

#define DEF_MISC_SKL_CATEGORY(symbol, name, events...) \
  const perfmon::EventId symbol##_events[] = {events};
#include "skylake-misc-categories.inc"

extern const CategorySpec kTargetCategories[] = {

// Fixed events.
#define DEF_FIXED_CATEGORY(symbol, name, events...)                     \
  {"cpu:" name, CategoryGroup::kFixedArch, 0, arraysize(symbol##_events), \
   &symbol##_events[0]},
#include "intel-pm-categories.inc"

// Architecturally specified programmable events.
#define DEF_ARCH_CATEGORY(symbol, name, events...)                             \
  {"cpu:" name, CategoryGroup::kProgrammableArch, 0, arraysize(symbol##_events), \
   &symbol##_events[0]},
#include "intel-pm-categories.inc"

// Model-specific misc events
#define DEF_MISC_SKL_CATEGORY(symbol, name, events...)                   \
  {"cpu:" name, CategoryGroup::kFixedModel, 0, arraysize(symbol##_events), \
   &symbol##_events[0]},
#include "skylake-misc-categories.inc"

// Model-specific programmable events.
#define DEF_SKL_CATEGORY(symbol, name, events...)     \
  {"cpu:" name, CategoryGroup::kProgrammableModel, 0, \
   arraysize(symbol##_events), &symbol##_events[0]},
#include "skylake-pm-categories.inc"

}; // kTargetCategories

extern const size_t kNumTargetCategories = arraysize(kTargetCategories);

extern const TimebaseSpec kTimebaseCategories[] = {
#define DEF_TIMEBASE_CATEGORY(symbol, name, event) {"cpu:" name, event},
#include "intel-timebase-categories.inc"
};

extern const size_t kNumTimebaseCategories = arraysize(kTimebaseCategories);

}  // namespace cpuperf_provider
