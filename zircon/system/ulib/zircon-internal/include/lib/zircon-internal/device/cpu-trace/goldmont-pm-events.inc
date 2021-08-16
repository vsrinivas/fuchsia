// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Goldmont non-architectural performance monitor registers.
// This has the benefit of providing the data in a usage-independent way.
// See: https://perfmon-events.intel.com/goldmont.html

#ifndef DEF_GLM_EVENT
#define DEF_GLM_EVENT(symbol, event_name, id, event, umask, flags, readable_name, description) \
  /* nothing */
#endif

#define GLM_ID_BASE (0x100)

// Non-architectural performance counters
// args:
// - symbol (used in code, must be unique for all symbols within an arch)
//   (must be VALID_C_SYMBOL)
// - event name (used in trace specification files)
//   (must be valid_c_symbol, without the group prefix,
//   i.e., instructions_retired not fixed_instructions_retired)
// - id (10 bits, must be unique for each DEF_GLM_EVENT entry)
//   (N.B. these values are very much not hardwired yet)
// - event (uint32_t)
// - umask (uint32_t)
// - flags (IPM_REG_FLAG_* values) (uint32_t)
// - readable name (ideally max 24 chars for legibility in display in chrome)
// - description (if empty use "")

DEF_GLM_EVENT(GLM_LD_BLOCKS_DATA_UNKNOWN,
  loads_blocked_by_unknown,
  GLM_ID_BASE + 0, 0x3, 0x1, IPM_REG_FLAG_PS,
  "Loads blocked by unknown",
  "Loads blocked by using a store forward because data was not available")
DEF_GLM_EVENT(GLM_LD_BLOCKS_STORE_FORWARD,
  loads_blocked_by_store,
  GLM_ID_BASE + 1, 0x03, 0x02, IPM_REG_FLAG_PS,
  "Loads blocked by store",
  "Loads blocked by using a store forward because of an address/size mismatch")
DEF_GLM_EVENT(GLM_LD_BLOCKS_4K_ALIAS,
  loads_blocked_by_4k_alias,
  GLM_ID_BASE + 2, 0x3, 0x4, IPM_REG_FLAG_PS,
  "Loads blocked by unknown",
  "Loads blocked by using a store forward because data was not available")

#undef DEF_GLM_EVENT
