// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cpuperf_provider/categories.h"

#include <zircon/device/cpu-trace/intel-pm.h>

#include "lib/fxl/logging.h"

namespace cpuperf_provider {

const IpmCategory kCategories[] = {
  // The categories from intel-pm-categories.inc must appear first for
  // |kProgrammableCategoryMap|.
#define DEF_CATEGORY(symbol, id, name, counters...) \
  { "cpu:" name, id },
#include "zircon/device/cpu-trace/intel-pm-categories.inc"
  {"cpu:fixed:all", (IPM_CATEGORY_FIXED_CTR0 |
                     IPM_CATEGORY_FIXED_CTR1 |
                     IPM_CATEGORY_FIXED_CTR2) },
  {"cpu:fixed:instructions_retired", IPM_CATEGORY_FIXED_CTR0 },
  {"cpu:fixed:unhalted_core_cycles", IPM_CATEGORY_FIXED_CTR1 },
  {"cpu:fixed:unhalted_reference_cycles", IPM_CATEGORY_FIXED_CTR2 },
  {"cpu:os", IPM_CATEGORY_OS},
  {"cpu:usr", IPM_CATEGORY_USR},
#if IPM_API_VERSION >= 2
  {"cpu:profile_pc", IPM_CATEGORY_PROFILE_PC},
#endif
  // Only one of the following is allowed.
  // TODO(dje): Error checking.
  {"cpu:tally", IPM_CATEGORY_TALLY},
  {"cpu:sample:1000", IPM_CATEGORY_SAMPLE_1000},
  {"cpu:sample:5000", IPM_CATEGORY_SAMPLE_5000},
  {"cpu:sample:10000", IPM_CATEGORY_SAMPLE_10000},
  {"cpu:sample:50000", IPM_CATEGORY_SAMPLE_50000},
  {"cpu:sample:100000", IPM_CATEGORY_SAMPLE_100000},
  {"cpu:sample:500000", IPM_CATEGORY_SAMPLE_500000},
  {"cpu:sample:1000000", IPM_CATEGORY_SAMPLE_1000000},
};

// Map programmable category ids to indices in kCategories.
const uint32_t kProgrammableCategoryMap[] = {
#define DEF_CATEGORY(symbol, id, name, counters...) \
  [id] = symbol,
#include "zircon/device/cpu-trace/intel-pm-categories.inc"
};

static_assert(countof(kProgrammableCategoryMap) <=
              IPM_CATEGORY_PROGRAMMABLE_MAX, "");

size_t GetNumCategories() {
  return countof(kCategories);
}

const IpmCategory& GetCategory(size_t cat) {
  FXL_DCHECK(cat < GetNumCategories());
  return kCategories[cat];
}

const IpmCategory& GetProgrammableCategoryFromId(uint32_t id) {
  FXL_DCHECK(id < countof(kProgrammableCategoryMap));
  if (id == 0)
    return kCategories[0];
  uint32_t cat = kProgrammableCategoryMap[id];
  // There can be gaps as ids aren't necessarily consecutive.
  // However |id| must have originally come from a lookup in kCategories.
  FXL_DCHECK(cat != 0);
  FXL_DCHECK(cat < GetNumCategories());
  return kCategories[cat];
}

uint64_t GetSampleFreq(uint32_t category_mask) {
  uint32_t mode = category_mask & IPM_CATEGORY_MODE_MASK;
  switch (mode) {
  case IPM_CATEGORY_TALLY: return 0;
  case IPM_CATEGORY_SAMPLE_1000: return 1000;
  case IPM_CATEGORY_SAMPLE_5000: return 5000;
  case IPM_CATEGORY_SAMPLE_10000: return 10000;
  case IPM_CATEGORY_SAMPLE_50000: return 50000;
  case IPM_CATEGORY_SAMPLE_100000: return 100000;
  case IPM_CATEGORY_SAMPLE_500000: return 500000;
  case IPM_CATEGORY_SAMPLE_1000000: return 1000000;
  default: FXL_NOTREACHED(); return 0;
  }
}

}  // namespace cpuperf_provider
