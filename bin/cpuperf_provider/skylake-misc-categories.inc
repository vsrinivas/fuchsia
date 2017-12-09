// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Table of categories for configuring perf collection of misc bits
// of Intel Skylake h/w.
// N.B. Unless otherwise noted, use of these categories in sampling mode
// requires provision of a separate "timebase" event to collect the data.
// See, e.g., desktop-6th-gen-core-family-datasheet-vol-2.

#ifndef DEF_MISC_SKL_CATEGORY
#error DEF_MISC_SKL_CATEGORY not defined
#endif

// Categories for fixed events
// Arguments:
// - symbol (must be unique among all categories)
// - name (must be unique among all categories,
//         cannot have any spaces: used in trace category name)
// - varargs list of event names (from intel-misc-events.inc)

DEF_MISC_SKL_CATEGORY(IPM_CATEGORY_MISC_MEM_BYTES,
  "mem:bytes",
  MISC_MEM_BYTES_READ,
  MISC_MEM_BYTES_WRITTEN)

DEF_MISC_SKL_CATEGORY(IPM_CATEGORY_MISC_MEM_REQUESTS,
  "mem:requests",
  MISC_MEM_GT_REQUESTS,
  MISC_MEM_IA_REQUESTS,
  MISC_MEM_IO_REQUESTS)

DEF_MISC_SKL_CATEGORY(IPM_CATEGORY_MISC_PKG_ACTIVE_CORE_CYCLES,
  "pkg:active_core_cycles",
  MISC_PKG_ALL_ACTIVE_CORE_CYCLES,
  MISC_PKG_ANY_ACTIVE_CORE_CYCLES)

DEF_MISC_SKL_CATEGORY(IPM_CATEGORY_MISC_PKG_ACTIVE_GT_CYCLES,
  "pkg:active_gt_cycles",
  MISC_PKG_ACTIVE_IA_GT_CYCLES,
  MISC_PKG_ACTIVE_GT_CYCLES,
  MISC_PKG_ACTIVE_GT_SLICE_CYCLES,
  MISC_PKG_ACTIVE_GT_ENGINE_CYCLES)

DEF_MISC_SKL_CATEGORY(IPM_CATEGORY_MISC_PKG_PECI_THERM_MARGIN,
  "pkg:peci_therm_margin",
  MISC_PKG_PECI_THERM_MARGIN)

DEF_MISC_SKL_CATEGORY(IPM_CATEGORY_MISC_PKG_RAPL_PERF_STATUS,
  "pkg:rapl_perf_status",
  MISC_PKG_RAPL_PERF_STATUS)

DEF_MISC_SKL_CATEGORY(IPM_CATEGORY_MISC_PKG_FREQ_CLAMPING_REASONS,
  "pkg:freq_clamping_reasons",
  MISC_PKG_IA_FREQ_CLAMPING_REASONS,
  MISC_PKG_GT_FREQ_CLAMPING_REASONS)

DEF_MISC_SKL_CATEGORY(IPM_CATEGORY_MISC_PKG_RP_FREQ_VOLT,
  "pkg:rp_freq_volt",
  MISC_PKG_RP_SLICE_FREQ,
  MISC_PKG_RP_UNSLICE_FREQ,
  MISC_PKG_RP_VOLT)

DEF_MISC_SKL_CATEGORY(IPM_CATEGORY_MISC_PKG_TEMP,
  "pkg:temp",
  MISC_PKG_EDRAM_TEMP,
  MISC_PKG_PKG_TEMP,
  MISC_PKG_IA_TEMP,
  MISC_PKG_GT_TEMP)

#undef DEF_MISC_SKL_CATEGORY
