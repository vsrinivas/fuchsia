// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Table of categories for configuring Intel Performance Monitor hardware.

#ifndef DEF_CATEGORY
#error "DEF_CATEGORY not defined"
#endif

// Arguments:
// - symbol
// - id (must be within IPM_CATEGORY_PROGRAMMABLE_MASK)
// - name (cannot have any spaces, used in trace category name)
// - varargs list of register names (from event .inc files)

// By convention NONE has id zero and name "none".
DEF_CATEGORY(IPM_CATEGORY_NONE, 0, "none")

DEF_CATEGORY(IPM_CATEGORY_ARCH_LLC, 1, "arch_llc",
  ARCH_LLC_REFERENCE,
  ARCH_LLC_MISSES)

DEF_CATEGORY(IPM_CATEGORY_ARCH_BRANCH, 2, "arch_branch",
  ARCH_BRANCH_INSTRUCTIONS_RETIRED,
  ARCH_BRANCH_MISSES_RETIRED)

// TODO(dje): Need a library to manage model categories.
// For now KISS and include them all here.
#define DEF_SKL_CATEGORY(symbol, id, name, ...) \
  DEF_CATEGORY(symbol, (10 + id), name, __VA_ARGS__)
#include <zircon/device/cpu-trace/skylake-pm-categories.inc>

#undef DEF_CATEGORY
