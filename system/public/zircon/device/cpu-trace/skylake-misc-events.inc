// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Database of miscellaneous Intel-specific performance monitor registers.
// This has the benefit of providing the data in a usage-independent way.
// See, e.g., desktop-6th-gen-core-family-datasheet-vol-2.

#ifndef DEF_MISC_SKL_EVENT
#error DEF_MISC_SKL_EVENT not defined
#endif

// Miscellaneous performance counters for Skylake
// args:
// - symbol (must be unique for all symbols within an arch)
// - id (10 bits, must be unique for each DEF_MISC_SKL_EVENT entry)
// - offset (the offset from BAR of the value)
// - size (in bits, either 32 or 64)
// - flags (IPM_MISC_REG_FLAG_* values) (uint32_t) [at present there are none]
// - short name (ideally max 24 chars for legibility in display in chrome)
// - description (if empty use "")
//
// These counters are kinda like fixed counters except there's no need to
// specify a h/w register number: there isn't any.
// Misc counter id rule: |id| must be <= CPUPERF_MAX_EVENT. 
//
// All current misc counters require a timebase, so that is the default
// for misc regs. This affects configuration: If one of these is requested
// in sampling mode and there is no timebase, then configuration fails.

DEF_MISC_SKL_EVENT(MISC_MEM_BYTES_READ,
  0, 0x5050, 32, 0,
  "Memory Bytes Read",    "64 byte lines read, from controller, recorded as bytes")
DEF_MISC_SKL_EVENT(MISC_MEM_BYTES_WRITTEN,
  1, 0x5054, 32, 0,
  "Memory Bytes Written", "64 byte lines written, from controller, recorded as bytes")

DEF_MISC_SKL_EVENT(MISC_MEM_GT_REQUESTS,
  2, 0x5040, 32, 0,
  "Memory requests from GT",
  "64 byte line requests to controller from GT, including partial requests that are later combined")
DEF_MISC_SKL_EVENT(MISC_MEM_IA_REQUESTS,
  3, 0x5044, 32, 0,
  "Memory requests from IA",
  "64 byte line requests to controller from IA, including partial requests that are later combined")
DEF_MISC_SKL_EVENT(MISC_MEM_IO_REQUESTS,
  4, 0x5048, 32, 0,
  "Memory requests from IO",
  "64 byte line requests to controller from IO, including partial requests that are later combined")

#undef DEF_MISC_SKL_EVENT
