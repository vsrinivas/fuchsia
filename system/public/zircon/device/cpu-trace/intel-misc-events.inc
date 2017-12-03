// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Database of miscellaneous Intel-specific performance monitor registers.
// This has the benefit of providing the data in a usage-independent way.

#ifndef DEF_MISC_EVENT
#define DEF_MISC_EVENT(symbol, id, flags, name, description) /* nothing */
#endif

// Miscellaneous performance counters
// args:
// - symbol (must be unique for all symbols within an arch)
// - id (10 bits, must be unique for each DEF_MISC_EVENT entry)
// - flags (IPM_MISC_REG_FLAG_* values) (uint32_t) [at present there are none]
// - short name (ideally max 24 chars for legibility in display in chrome)
// - description (if empty use "")
//
// Misc counter id rule: They start at zero and there are no holes (excepting
// counters that get deprecated). These counters are kinda like fixed counters
// except there's no need to specify a h/w register number: there isn't any.
// |id| must be < IPM_MAX_MISC_COUNTERS.
//
// All current misc counters require a timebase, so that is the default
// for misc regs. This affects configuration: If one of these is requested
// in sampling mode and there is no timebase, then configuration fails.

DEF_MISC_EVENT(MISC_MEM_BYTES_READ,    0, 0,
  "Memory Bytes Read",    "64 byte lines read, from controller, recorded as bytes")
DEF_MISC_EVENT(MISC_MEM_BYTES_WRITTEN, 1, 0,
  "Memory Bytes Written", "64 byte lines written, from controller, recorded as bytes")

DEF_MISC_EVENT(MISC_MEM_GT_REQUESTS,   2, 0,
  "Memory requests from GT",
  "64 byte line requests to controller from GT, including partial requests that are later combined")
DEF_MISC_EVENT(MISC_MEM_IA_REQUESTS,   3, 0,
  "Memory requests from IA",
  "64 byte line requests to controller from IA, including partial requests that are later combined")
DEF_MISC_EVENT(MISC_MEM_IO_REQUESTS,   4, 0,
  "Memory requests from IO",
  "64 byte line requests to controller from IO, including partial requests that are later combined")

#undef DEF_MISC_EVENT
