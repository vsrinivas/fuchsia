// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ARM64 architecture-specified performance monitor registers.
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

DEF_FIXED_EVENT(FIXED_CYCLE_COUNTER,
  cycle_counter,
  1, 0, 0,
  "Cycle Counter",
  "")

// Architectural performance counters
// args:
// - symbol
// - event name (used in trace specification files)
//   (must be valid_c_symbol, without the group prefix,
//   i.e., instructions_retired not arch_instructions_retired)
// - id (10 bits, must be unique for each DEF_ARCH_EVENT entry)
// - PMCEIDn bit position (uint32_t, extending over several registers)
//   0-31 = PMCEID0 and so on
// - event (uint32_t)
// - flags (ARM64_PMU_REG_FLAG_* values) (uint32_t)
// - readable name (ideally max 24 chars for legibility in display in chrome)
// - description (if empty use "")
//
// ARM "architectural" performance counters are a bit squishy.
// Only a subset of these are "architectural" as specified in the ARM docs,
// E.g., "ARM Architecture Reference Manual ARMv8",
// chapter D6 "The Performance Monitors Extension".
// The others are "microarchitectural". And then of the architectural counters
// only a subset of those are required. All these counters are listed here,
// at least for the time being, as the reference manual provides definitions of
// them all (as opposed to them being defined in a model-specific manual.
// The reference manual also lists some architectural events as "required".
// These are not currently marked so. It's early. This will evolve as needed.

#ifndef DEF_ARCH_EVENT
#define DEF_ARCH_EVENT(symbol, event_name, id, pmceid_bit, event, flags, readable_name, description) /* nothing */
#endif

// This macro is internal to this file and captures the commonality across
// all the architectural events.
#define DEF_ARM64_ARCH_EVENT(symbol, name, num, readable_name, description) \
  DEF_ARCH_EVENT(symbol, name, (num) + 1, (num), (num), \
                 ARM64_PMU_REG_FLAG_ARCH, readable_name, description)

// This macro is internal to this file and captures the commonality across
// all the micro-architectural events.
#define DEF_ARM64_MICROARCH_EVENT(symbol, name, num, readable_name, description) \
  DEF_ARCH_EVENT(symbol, name, (num) + 1, (num), (num), \
                 ARM64_PMU_REG_FLAG_MICROARCH, readable_name, description)

DEF_ARM64_ARCH_EVENT(ARCH_SW_INCR,
  sw_incr,
  0,
  "SW Increment",
  "")

DEF_ARM64_MICROARCH_EVENT(ARCH_L1I_CACHE_REFILL,
  l1i_cache_refill,
  1,
  "L1I Cache Refill",
  "Attributable L1 instruction cache refill")

DEF_ARM64_MICROARCH_EVENT(ARCH_L1I_TLB_REFILL,
  l1i_tlb_refill,
  2,
  "L1I TLB Refill",
  "Attributable L1 instruction TLB refill")

DEF_ARM64_MICROARCH_EVENT(ARCH_L1D_CACHE,
  l1d_cache,
  3,
  "L1D Cache",
  "Attributable L1 data cache access")

DEF_ARM64_MICROARCH_EVENT(ARCH_L1D_CACHE_REFILL,
  l1d_cache_refill,
  4,
  "L1D Cache Refill",
  "Attributable L1 data cache refill")

DEF_ARM64_MICROARCH_EVENT(ARCH_L1D_TLB_REFILL,
  l1d_tlb_refill,
  5,
  "L1D TLB Refill",
  "Attributable L1 data TLB refill")

DEF_ARM64_ARCH_EVENT(ARCH_LD_RETIRED,
  ld_retired,
  6,
  "Loads Retired",
  "Load instructions retired")

DEF_ARM64_ARCH_EVENT(ARCH_ST_RETIRED,
  st_retired,
  7,
  "Stores Retired",
  "Store instructions retired")

DEF_ARM64_ARCH_EVENT(ARCH_INST_RETIRED,
  inst_retired,
  8,
  "Instructions Retired",
  "Instructions architecturally executed")

DEF_ARM64_ARCH_EVENT(ARCH_EXC_TAKEN,
  exc_taken,
  9,
  "Exceptions Taken",
  "Exceptions taken")

DEF_ARM64_ARCH_EVENT(ARCH_EXC_RETURN,
  exc_return,
  10,
  "Exceptions Returned",
  "Instruction architecturally executed, condition code check passed, exception return")

DEF_ARM64_ARCH_EVENT(ARCH_CID_WRITE_RETIRED,
  cid_write_retired,
  11,
  "CID Write Retired",
  "Instruction architecturally executed, condition code check passed, write to CONTEXTIDR")

DEF_ARM64_ARCH_EVENT(ARCH_PC_WRITE_RETIRED,
  pc_write_retired,
  12,
  "PC Write Retired",
  "Instruction architecturally executed, condition code check passed, software change of the PC")

DEF_ARM64_ARCH_EVENT(ARCH_BR_IMMED_RETIRED,
  br_immed_retired,
  13,
  "Immediate branches Retired",
  "Instruction architecturally executed, immediate branch")

DEF_ARM64_ARCH_EVENT(ARCH_BR_RETURN_RETIRED,
  br_return_retired,
  14,
  "Return Instructions",
  "Instruction architecturally executed, condition code check passed, procedure return")

DEF_ARM64_ARCH_EVENT(ARCH_UNALIGNED_LDST_RETIRED,
  unaligned_ldst_retired,
  15,
  "Unaligned Loads/Stores Retired",
  "Instruction architecturally executed, condition code check passed, unaligned load or store")

DEF_ARM64_MICROARCH_EVENT(ARCH_BR_MIS_PRED,
  br_mis_pred,
  16,
  "Mispredicted branches",
  "Mispredicted or not predicted branch speculatively executed")

DEF_ARM64_MICROARCH_EVENT(ARCH_CPU_CYCLES,
  cpu_cycles,
  17,
  "CPU Cycles",
  "CPU Cycles")

DEF_ARM64_MICROARCH_EVENT(ARCH_BR_PRED,
  br_pred,
  18,
  "Predictable branches",
  "Predictable branch speculatively executed")

DEF_ARM64_MICROARCH_EVENT(ARCH_MEM_ACCESS,
  mem_access,
  19,
  "Data Memory Access",
  "Data memory access")

DEF_ARM64_MICROARCH_EVENT(ARCH_L1I_CACHE,
  l1i_cache,
  20,
  "L1I Cache",
  "L1 instruction cache access")

DEF_ARM64_MICROARCH_EVENT(ARCH_L1D_CACHE_WB,
  l1d_cache_wb,
  21,
  "L1D Writeback",
  "L1 data cache writeback")

DEF_ARM64_MICROARCH_EVENT(ARCH_L2D_CACHE,
  l2d_cache,
  22,
  "L2D Cache",
  "L2 data cache access")

DEF_ARM64_MICROARCH_EVENT(ARCH_L2D_CACHE_REFILL,
  l2d_cache_refill,
  23,
  "L2D Cache Refill",
  "L2 data cache refill")

DEF_ARM64_MICROARCH_EVENT(ARCH_L2D_CACHE_WB,
  l2d_cache_wb,
  24,
  "L2D Writeback",
  "L2 data cache writeback")

DEF_ARM64_MICROARCH_EVENT(ARCH_BUS_ACCESS,
  bus_access,
  25,
  "Bus Access",
  "Bus access")

DEF_ARM64_MICROARCH_EVENT(ARCH_MEMORY_ERROR,
  memory_error,
  26,
  "Memory Error",
  "Local memory error")

DEF_ARM64_MICROARCH_EVENT(ARCH_INST_SPEC,
  inst_spec,
  27,
  "Insn speculatively exec",
  "Operation speculatively executed")

DEF_ARM64_ARCH_EVENT(ARCH_TTBR_WRITE_RETIRED,
  ttbr_write_retired,
  28,
  "Writes to TTBR",
  "Instruction architecturally executed, condition code check passed, write to TTBR")

DEF_ARM64_MICROARCH_EVENT(ARCH_BUS_CYCLES,
  bus_cycles,
  29,
  "Bus Cycles",
  "Bus cycles")

#if 0 // TODO(dje): Not yet supported, so don't list it.
DEF_ARM64_ARCH_EVENT(ARCH_CHAIN,
  chain,
  30,
  "Chain",
  "For odd-numbered counters, increments the count by one for each overflow of the preceding even-numbered counter. For even-numbered counters there is no increment.")
#endif

DEF_ARM64_MICROARCH_EVENT(ARCH_L1D_CACHE_ALLOCATE,
  l1d_cache_allocate,
  31,
  "L1 Data Cache Allocations",
  "L1 data cache allocation without refill")

DEF_ARM64_MICROARCH_EVENT(ARCH_L2D_CACHE_ALLOCATE,
  l2d_cache_allocate,
  32,
  "L2 Data Cache Allocations",
  "L2 data cache allocation without refill")

#undef DEF_FIXED_EVENT
#undef DEF_ARCH_EVENT
#undef DEF_ARM64_ARCH_EVENT
#undef DEF_ARM64_MICROARCH_EVENT
