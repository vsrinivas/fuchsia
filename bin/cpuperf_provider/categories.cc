// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cpuperf_provider/categories.h"

#include <zircon/device/intel-pm.h>

#include "lib/fxl/logging.h"

namespace cpuperf_provider {

constexpr IpmCategory kCategories[] = {
#define DEF_CATEGORY(symbol, ordinal, name, counters...) \
  { "cpu:" name, ordinal },
#include "zircon/device/intel-pm-categories.inc"
  // These are last so that perf_event_category_t values can index
  // |kCategories|.
  {"cpu:fixed", IPM_CATEGORY_FIXED},
  {"cpu:os", IPM_CATEGORY_OS},
  {"cpu:usr", IPM_CATEGORY_USR},
  // Only one of the following is allowed.
  // TODO(dje): Error checking.
  {"cpu:tally", IPM_CATEGORY_COUNT},
  {"cpu:sample-1000", IPM_CATEGORY_SAMPLE_1000},
  {"cpu:sample-10000", IPM_CATEGORY_SAMPLE_10000},
  {"cpu:sample-100000", IPM_CATEGORY_SAMPLE_100000},
  {"cpu:sample-1000000", IPM_CATEGORY_SAMPLE_1000000},
};

size_t GetNumCategories() {
  return countof(kCategories);
}

const IpmCategory& GetCategory(size_t cat) {
  FXL_DCHECK(cat < GetNumCategories());
  return kCategories[cat];
}

uint64_t GetSampleFreq(uint32_t category_mask) {
  uint32_t mode = category_mask & IPM_CATEGORY_MODE_MASK;
  switch (mode) {
  case IPM_CATEGORY_COUNT: return 0;
  case IPM_CATEGORY_SAMPLE_1000: return 1000;
  case IPM_CATEGORY_SAMPLE_10000: return 10000;
  case IPM_CATEGORY_SAMPLE_100000: return 100000;
  case IPM_CATEGORY_SAMPLE_1000000: return 1000000;
  default: FXL_NOTREACHED(); return 0;
  }
}

}  // namespace cpuperf_provider
