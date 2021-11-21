// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

DEF_FIXED_CATEGORY(RISCV64_CATEGORY_FIXED_CYCLE_COUNTER,
    "fixed:cycle_counter",
    FIXED_CYCLE_COUNTER)

#undef DEF_FIXED_CATEGORY
#undef DEF_ARCH_CATEGORY
