// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// RISCV64 architecture-specified performance monitor registers.
// This has the benefit of providing the data in a usage-independent way.
//
// The includer should define two macros: DEF_FIXED_EVENT, DEF_ARCH_EVENT.
// They are documented below.
// If not defined the default is to ignore the affected counters.

// Fixed counters
// There is only one fixed counter on ARM: The cycle counter.
// The format here is equivalent to the Intel format for consistency and
// for future expansion concerns.
//
// args:
// - symbol (used in code, must be unique for all symbols within an arch)
//   (must be VALID_C_SYMBOL)
// - event name (used in trace specification files)
//   (must be valid_c_symbol, without the group prefix,
//   i.e., instructions_retired not fixed_instructions_retired)
// - id (10 bits, must be unique for each DEF_FIXED_EVENT entry)
// - event number as known to hardware
// - flags (uint32_t) [at present there are none]
// - readable name (ideally max 24 chars for legibility in display in chrome)
// - description (if empty use "")

#ifndef DEF_FIXED_EVENT
#define DEF_FIXED_EVENT(symbol, event_name, id, regnum, flags, readable_name, description) /* nothing */
#endif

#ifndef DEF_ARCH_EVENT
#define DEF_ARCH_EVENT(symbol, event_name, id, pmceid_bit, event, flags, readable_name, description) /* nothing */
#endif

DEF_FIXED_EVENT(FIXED_CYCLE_COUNTER,
  cycle_counter,
  1, 0, 0,
  "Cycle Counter",
  "")

#undef DEF_FIXED_EVENT
#undef DEF_ARCH_EVENT
