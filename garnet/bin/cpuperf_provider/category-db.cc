// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>

#include "garnet/bin/cpuperf_provider/categories.h"
#include "garnet/lib/perfmon/events.h"

namespace cpuperf_provider {

const CategorySpec kCommonCategories[] = {
    // Options
    {"cpu:os", CategoryGroup::kOption,
     static_cast<CategoryValue>(TraceOption::kOs), 0, nullptr},
    {"cpu:user", CategoryGroup::kOption,
     static_cast<CategoryValue>(TraceOption::kUser), 0, nullptr},
    {"cpu:pc", CategoryGroup::kOption,
     static_cast<CategoryValue>(TraceOption::kPc), 0, nullptr},
#ifdef __x86_64__
    {"cpu:last_branch",   CategoryGroup::kOption,
     static_cast<CategoryValue>(TraceOption::kLastBranch), 0, nullptr},
#endif

// Sampling rates.
// Only one of the following is allowed.
#define DEF_SAMPLE(name, value) \
  { "cpu:" name, CategoryGroup::kSample, value, 0, nullptr }
    DEF_SAMPLE("tally", 0),
    DEF_SAMPLE("sample:100", 100),
    DEF_SAMPLE("sample:500", 500),
    DEF_SAMPLE("sample:1000", 1000),
    DEF_SAMPLE("sample:5000", 5000),
    DEF_SAMPLE("sample:10000", 10000),
    DEF_SAMPLE("sample:50000", 50000),
    DEF_SAMPLE("sample:100000", 100000),
    DEF_SAMPLE("sample:500000", 500000),
    DEF_SAMPLE("sample:1000000", 1000000),
#undef DEF_SAMPLE
};

const size_t kNumCommonCategories = fbl::count_of(kCommonCategories);

}  // namespace cpuperf_provider
