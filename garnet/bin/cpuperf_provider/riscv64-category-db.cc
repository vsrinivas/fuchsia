// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iterator>

#include "garnet/bin/cpuperf_provider/riscv64-events.h"
#include "garnet/bin/cpuperf_provider/categories.h"

namespace cpuperf_provider {

#define DEF_FIXED_CATEGORY(symbol, name, events...) \
  const perfmon::EventId symbol##_events[] = {events};
#define DEF_ARCH_CATEGORY(symbol, name, events...) \
  const perfmon::EventId symbol##_events[] = {events};
#include "riscv64-pm-categories.inc"

extern const CategorySpec kTargetCategories[] = {

// Fixed events.
#define DEF_FIXED_CATEGORY(symbol, name, events...) \
  {"cpu:" name, CategoryGroup::kFixedArch, 0, std::size(symbol##_events), &symbol##_events[0]},
#include "riscv64-pm-categories.inc"

// Architecturally specified programmable events.
#define DEF_ARCH_CATEGORY(symbol, name, events...)                               \
  {"cpu:" name, CategoryGroup::kProgrammableArch, 0, std::size(symbol##_events), \
   &symbol##_events[0]},
#include "riscv64-pm-categories.inc"

};  // kTargetCategories

extern const size_t kNumTargetCategories = std::size(kTargetCategories);

extern const TimebaseSpec kTimebaseCategories[] = {
#define DEF_TIMEBASE_CATEGORY(symbol, name, event) {"cpu:" name, event},
#include "riscv64-timebase-categories.inc"
};

extern const size_t kNumTimebaseCategories = std::size(kTimebaseCategories);

}  // namespace cpuperf_provider
