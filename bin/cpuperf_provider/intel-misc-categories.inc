// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Table of categories for configuring perf collection of misc bits
// of Intel h/w.

#ifndef DEF_MISC_CATEGORY
#define DEF_MISC_CATEGORY(symbol, name, events...)
#endif

// Categories for fixed events
// Arguments:
// - symbol (must be unique among all categories)
// - name (must be unique among all categories,
//         cannot have any spaces: used in trace category name)
// - varargs list of event names (from intel-misc-events.inc)

// N.B. In sampling mode this category requires provision of a separate
// "timebase" event to use to collect the data.
DEF_MISC_CATEGORY(IPM_CATEGORY_MISC_MEM_BYTES,
  "mem:bytes",
  MISC_MEM_BYTES_READ,
  MISC_MEM_BYTES_WRITTEN)

// N.B. In sampling mode this category requires provision of a separate
// "timebase" event to use to collect the data.
DEF_MISC_CATEGORY(IPM_CATEGORY_MISC_MEM_REQUESTS,
  "mem:requests",
  MISC_MEM_GT_REQUESTS,
  MISC_MEM_IA_REQUESTS,
  MISC_MEM_IO_REQUESTS)

#undef DEF_MISC_CATEGORY
