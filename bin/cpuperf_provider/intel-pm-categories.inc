// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Table of categories for configuring Intel Performance Monitor hardware.

#ifndef DEF_FIXED_CATEGORY
#define DEF_FIXED_CATEGORY(symbol, name, events...)
#endif

#ifndef DEF_ARCH_CATEGORY
#define DEF_ARCH_CATEGORY(symbol, name, events...)
#endif

// Categories for fixed events
// Arguments:
// - symbol (must be unique among all categories)
// - name (must be unique among all categories,
//         cannot have any spaces: used in trace category name)
// - varargs list of register names (from event .inc files)

DEF_FIXED_CATEGORY(IPM_CATEGORY_FIXED_INSTRUCTIONS_RETIRED,
  "fixed:instructions_retired",
  FIXED_INSTRUCTIONS_RETIRED)

DEF_FIXED_CATEGORY(IPM_CATEGORY_FIXED_UNHALTED_CORE_CYCLES,
  "fixed:unhalted_core_cycles",
  FIXED_UNHALTED_CORE_CYCLES)

DEF_FIXED_CATEGORY(IPM_CATEGORY_FIXED_UNHALTED_REFERENCE_CYCLES,
  "fixed:unhalted_reference_cycles",
  FIXED_UNHALTED_REFERENCE_CYCLES)

// Categories for architecturally-defined events
// Arguments:
// - symbol (must be unique among all categories)
// - name (must be unique among all categories,
//         cannot have any spaces: used in trace category name)
// - varargs list of register names (from event .inc files)

DEF_ARCH_CATEGORY(IPM_CATEGORY_ARCH_LLC, "arch:llc",
  ARCH_LLC_REFERENCE,
  ARCH_LLC_MISSES)

DEF_ARCH_CATEGORY(IPM_CATEGORY_ARCH_BRANCH, "arch:branch",
  ARCH_BRANCH_INSTRUCTIONS_RETIRED,
  ARCH_BRANCH_MISSES_RETIRED)

#undef DEF_FIXED_CATEGORY
#undef DEF_ARCH_CATEGORY
