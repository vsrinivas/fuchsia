// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Table of categories for configuring Intel Performance Monitor hardware.

#ifndef DEF_FIXED_CATEGORY
#define DEF_FIXED_CATEGORY(symbol, id, name, events...)
#endif

#ifndef DEF_ARCH_CATEGORY
#define DEF_ARCH_CATEGORY(symbol, id, name, events...)
#endif

// Categories for fixed events
// Arguments:
// - symbol (must be unique among all categories)
// - id (must correspond to h/w event id)
// - name (must be unique among all categories,
//         cannot have any spaces: used in trace category name)
// - varargs list of register names (from event .inc files)

DEF_FIXED_CATEGORY(IPM_CATEGORY_FIXED_INSTRUCTIONS_RETIRED, 0,
  "fixed:instructions_retired",
  FIXED_INSTRUCTIONS_RETIRED)

DEF_FIXED_CATEGORY(IPM_CATEGORY_FIXED_UNHALTED_CORE_CYCLES, 1,
  "fixed:unhalted_core_cycles",
  FIXED_UNHALTED_CORE_CYCLES)

DEF_FIXED_CATEGORY(IPM_CATEGORY_FIXED_UNHALTED_REFERENCE_CYCLES, 2,
  "fixed:unhalted_reference_cycles",
  FIXED_UNHALTED_REFERENCE_CYCLES)

// Categories for architecturally-defined events
// Arguments:
// - symbol (must be unique among all categories)
// - id (must be unique within DEF_ARCH_CATEGORY)
// - name (must be unique among all categories,
//         cannot have any spaces: used in trace category name)
// - varargs list of register names (from event .inc files)

// By convention NONE has id zero and name "none".
DEF_ARCH_CATEGORY(IPM_CATEGORY_ARCH_NONE, 0, "arch:none")

DEF_ARCH_CATEGORY(IPM_CATEGORY_ARCH_LLC, 1, "arch:llc",
  ARCH_LLC_REFERENCE,
  ARCH_LLC_MISSES)

DEF_ARCH_CATEGORY(IPM_CATEGORY_ARCH_BRANCH, 2, "arch:branch",
  ARCH_BRANCH_INSTRUCTIONS_RETIRED,
  ARCH_BRANCH_MISSES_RETIRED)

#undef DEF_FIXED_CATEGORY
#undef DEF_ARCH_CATEGORY
