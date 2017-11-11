// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Database of performance monitor registers.
// This has the benefit of providing the data in a usage-independent way.

// TODO(dje): Revisit where things should live once things settle.

#ifndef DEF_ARCH_EVENT
#define DEF_ARCH_EVENT(symbol, cpuid_ebx_bit, event, umask, flags, description) /* nothing */
#endif

#ifndef DEF_SKL_EVENT
#define DEF_SKL_EVENT(symbol, event, umask, flags, description) /* nothing */
#endif

// Architectural performance counters
// args:
// - symbol
// - CPUID.AH.EBX bit position (uint32_t)
// - event (uint32_t)
// - umask (uint32_t)
// - flags (IPM_REG_FLAG_* values) (uint32_t)
// - description

DEF_ARCH_EVENT(ARCH_UNHALTED_CORE_CYCLES,        0, 0x3c, 0x00, IPM_REG_FLAG_ARCH | IPM_REG_FLAG_FIXED1, "UnHalted Core Cycles")
DEF_ARCH_EVENT(ARCH_INSTRUCTIONS_RETIRED,        1, 0xc0, 0x00, IPM_REG_FLAG_ARCH | IPM_REG_FLAG_FIXED0, "Instructions Retired")
DEF_ARCH_EVENT(ARCH_UNHALTED_REFERENCE_CYCLES,   2, 0x3c, 0x01, IPM_REG_FLAG_ARCH | IPM_REG_FLAG_FIXED2, "UnHalted Reference Cycles")
DEF_ARCH_EVENT(ARCH_LLC_REFERENCE,               3, 0x2e, 0x4f, IPM_REG_FLAG_ARCH, "LLC (Last Level Cache) References")
DEF_ARCH_EVENT(ARCH_LLC_MISSES,                  4, 0x2e, 0x41, IPM_REG_FLAG_ARCH, "LLC (Last Level Cache) Misses")
DEF_ARCH_EVENT(ARCH_BRANCH_INSTRUCTIONS_RETIRED, 5, 0xc4, 0x00, IPM_REG_FLAG_ARCH, "Branch Instructions Retired")
DEF_ARCH_EVENT(ARCH_BRANCH_MISSES_RETIRED,       6, 0xc5, 0x00, IPM_REG_FLAG_ARCH, "Branch Misses Retired")

#undef DEF_ARCH_EVENT

// Non-architectural performance counters
// args:
// - symbol
// - event (uint32_t)
// - umask (uint32_t)
// - flags (IPM_REG_FLAG_* values) (uint32_t)
// - description

// skylake/kabylake non-arch icache events
// TODO(dje): Eventually it will make sense to move each micro-arch
// to their own file.

DEF_SKL_EVENT(SKL_LD_BLOCKS_STORE_FORWARD,        0x03, 0x02, 0,
  "Loads blocked by overlapping with store buffer that cannot be forwarded")
DEF_SKL_EVENT(SKL_LD_BLOCKS_NO_SR,                0x03, 0x08, 0,
  "Number of times that split load operations are temporarily blocked because all resources for handling the split are in use")
DEF_SKL_EVENT(SKL_LD_BLOCKS_PARTIAL_ADDRESS_ALIAS, 0x07, 0x01, 0,
  "False dependencies in MOB due to partial compare on address")

DEF_SKL_EVENT(SKL_DTLB_LOAD_MISSES_MISS_CAUSES_A_WALK, 0x08, 0x01, 0,
  "Load misses in all TLB levels that cause a page walk")
DEF_SKL_EVENT(SKL_DTLB_LOAD_MISSES_WALK_COMPLETED, 0x08, 0x0e, 0,
  "Load misses in all TBL levels causes a page walk that completes")
DEF_SKL_EVENT(SKL_DTLB_LOAD_MISSES_WALK_PENDING, 0x08, 0x10, 0,
  "Counts 1 per cycle for each PMH that is busy with a page walk for a load")
DEF_SKL_EVENT(SKL_DTLB_LOAD_MISSES_WALK_ACTIVE, 0x08, 0x10, IPM_REG_FLAG_CMSK1,
  "Counts 1 per cycle for each PMH that is busy with a page walk for a load")
DEF_SKL_EVENT(SKL_DTLB_LOAD_MISSES_STLB_HIT, 0x08, 0x20, 0,
  "Loads that miss the DTBL but hit STLB")

DEF_SKL_EVENT(SKL_INT_MISC_RECOVERY_CYCLES, 0x0d, 0x01, 0,
  "Core cycles the allocator was stalled due to recovery from earlier machine clear event for this thread")
DEF_SKL_EVENT(SKL_INT_MISC_RECOVERY_CYCLES_ANY, 0x0d, 0x01, IPM_REG_FLAG_ANYT,
  "Core cycles the allocator was stalled due to recovery from earlier machine clear event for any logical thread on this processor core")
DEF_SKL_EVENT(SKL_INT_MISC_CLEAR_RESTEER_CYCLES, 0x0d, 0x80, 0,
  "Cycles the issue-stage is waiting for front end to fetch from re-steered path following branch misprediction or machine clear events")

// TODO(dje): UOPS_*
// TODO(dje): ARITH_*

DEF_SKL_EVENT(SKL_L2_RQSTS_DEMAND_DATA_RD_MISS, 0x24, 0x21, 0,
  "Demand Data Read requests that missed L2, no rejects")
DEF_SKL_EVENT(SKL_L2_RQSTS_RFO_MISS, 0x24, 0x22, 0,
  "RFO requests that missed L2")
DEF_SKL_EVENT(SKL_L2_RQSTS_CODE_RD_MISS, 0x24, 0x24, 0,
  "L2 misses when fetching instructions")
DEF_SKL_EVENT(SKL_L2_RQSTS_ALL_DEMAND_MISS, 0x24, 0x27, 0,
  "Demand requests that missed L2")
DEF_SKL_EVENT(SKL_L2_RQSTS_PF_MISS, 0x24, 0x38, 0,
  "Requests from L1/L2/L3 h/w prefetchers or load s/w prefetches that miss L2")
DEF_SKL_EVENT(SKL_L2_RQSTS_MISS, 0x24, 0x3f, 0,
  "All requests that missed L2")
DEF_SKL_EVENT(SKL_L2_RQSTS_DEMAND_DATA_RD_HIT, 0x24, 0x41, 0,
  "Demand Data Read requests that hit L2")
DEF_SKL_EVENT(SKL_L2_RQSTS_RFO_HIT, 0x24, 0x42, 0,
  "RFO requests that hit L2")
DEF_SKL_EVENT(SKL_L2_RQSTS_CODE_RD_HIT, 0x24, 0x44, 0,
  "L2 hits when fetching instructions")
DEF_SKL_EVENT(SKL_L2_RQSTS_PF_HIT, 0x24, 0xd8, 0,
  "Prefetches that hit L2")
DEF_SKL_EVENT(SKL_L2_RQSTS_ALL_DEMAND_DATA_RD, 0x24, 0xe1, 0,
  "All Demand Data Read requests to L2")
DEF_SKL_EVENT(SKL_L2_RQSTS_ALL_RFO, 0x24, 0xe2, 0,
  "All RFO requests to L2")
DEF_SKL_EVENT(SKL_L2_RQSTS_ALL_CODE_RD, 0x24, 0xe4, 0,
  "All L2 code requests")
DEF_SKL_EVENT(SKL_L2_RQSTS_ALL_DEMAND_REFERENCES, 0x24, 0xe7, 0,
  "All demand requests to L2")
DEF_SKL_EVENT(SKL_L2_RQSTS_ALL_PF, 0x24, 0xf8, 0,
  "All requests from L1/L2/L3 h/w prefetchers or load s/w prefetches")
DEF_SKL_EVENT(SKL_L2_RQSTS_REFERENCES, 0x24, 0xef, 0,
  "All requests to L2")

DEF_SKL_EVENT(SKL_LONGEST_LAT_CACHE_REFERENCE, 0x2e, 0x4f, 0,
  "Requests originating from core that reference cache line in L3")
DEF_SKL_EVENT(SKL_LONGEST_LAT_CACHE_MISS, 0x2e, 0x41, 0,
  "Cache miss condition for references to L3")

// TODO(dje): CPU_CLK_*

DEF_SKL_EVENT(SKL_L1D_PEND_MISS_PENDING, 0x48, 0x01, 0,
  "Number of outstanding L1D misses every cycle")
DEF_SKL_EVENT(SKL_L1D_PEND_MISS_PENDING_CYCLES, 0x48, 0x01, IPM_REG_FLAG_CMSK1,
  "Number of outstanding L1D misses for this logical processor")
DEF_SKL_EVENT(SKL_L1D_PEND_MISS_PENDING_CYCLES_ANY, 0x48, 0x01, IPM_REG_FLAG_CMSK1 | IPM_REG_FLAG_ANYT,
  "Number of outstanding L1D misses for any logical thread on this processor core")
DEF_SKL_EVENT(SKL_L1D_PEND_MISS_FB_FULL, 0x48, 0x02, 0,
  "Number of times a request needed an FB entry but no entry was available")

DEF_SKL_EVENT(SKL_DTLB_STORE_MISSES_MISS_CAUSES_A_WALK, 0x49, 0x01, 0,
  "Store misses in all TBL levels that cause page walks")
DEF_SKL_EVENT(SKL_DTLB_STORE_MISSES_WALK_COMPLETED, 0x49, 0x0e, 0,
  "Completed page walks in any TLB levels due to store misses")
DEF_SKL_EVENT(SKL_DTLB_STORE_MISSES_WALK_PENDING, 0x49, 0x10, 0,
  "Counts 1 per cycle for each PMH that is busy with a page walk for a store")
DEF_SKL_EVENT(SKL_DTLB_STORE_MISSES_WALK_ACTIVE, 0x49, 0x10, IPM_REG_FLAG_CMSK1,
  "Cycles when at least one PMH is busy with a page walk for a store")
DEF_SKL_EVENT(SKL_DTLB_STORE_MISSES_STLB_HIT, 0x49, 0x20, 0,
  "Store misses that missed DTLB but hit STLB")

// TODO(dje): LOAD_HIT_PRE_HW_PF, EPT_WALK_PENDING

DEF_SKL_EVENT(SKL_L1D_REPLACEMENT, 0x51, 0x01, 0,
  "Number of lines brought into L1 data cache")

// TODO(dje): RS_*

DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_OUTSTANDING_DEMAND_DATA_RD, 0x60, 0x01, 0,
  "Incremented each cycle of the number of offcore outstanding Demand Data Read transactions in SQ to uncore")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_OUTSTANDING_CYCLES_WITH_DEMAND_DATA_RD, 0x60, 0x01, IPM_REG_FLAG_CMSK1,
  "Cycles with at least 1 offcore outstanding Demand Data Read transactions in SQ to uncore")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_OUTSTANDING_CYCLES_WITH_DEMAND_DATA_RD_GE_6, 0x60, 0x01, IPM_REG_FLAG_CMSK6,
  "Cycles with at least 6 offcore outstanding Demand Data Read transactions in SQ to uncore")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_OUTSTANDING_DEMAND_CODE_RD, 0x60, 0x02, 0,
  "Incremented each cycle of the number of offcore outstanding Demand Code Read transactions in SQ to uncore") 
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_OUTSTANDING_CYCLES_WITH_DEMAND_CODE_RD, 0x60, 0x02, IPM_REG_FLAG_CMSK1,
  "Cycles with at least 1 offcore outstanding Demand Code Read transactions in SQ to uncore")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_OUTSTANDING_DEMAND_RFO, 0x60, 0x04, 0,
  "Incremented each cycle of the number of offcore outstanding RFO store transactions in SQ to uncore")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_OUTSTANDING_CYCLES_WITH_DEMAND_RFO, 0x60, 0x04, IPM_REG_FLAG_CMSK1,
  "Cycles with at least 1 offcore outstanding RFO transactions in SQ to uncore")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_OUTSTANDING_DATA_RD, 0x60, 0x08, 0,
  "Incremented each cycle of the number of offcore outstanding data read transactions in SQ to uncore")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_OUTSTANDING_CYCLES_WITH_DATA_RD, 0x60, 0x08, IPM_REG_FLAG_CMSK1,
  "Cycles with at least 1 offcore outstanding data read transactions in SQ to uncore")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_OUTSTANDING_L3_MISS_DEMAND_DATA_RD, 0x60, 0x10, 0,
  "Incremented each cycle of the number of offcore outstanding Demand Data Read requests from SQ that missed L3")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_OUTSTANDING_CYCLES_WITH_L3_MISS_DEMAND_DATA_RD, 0x60, 0x10, IPM_REG_FLAG_CMSK1,
  "Cycles with at least 1 offcore outstanding Demand Data Read request from SQ that missed L3")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_OUTSTANDING_CYCLES_WITH_L3_MISS_DEMAND_DATA_RD_GE_6, 0x60, 0x10, IPM_REG_FLAG_CMSK6,
  "Cycles with at least 6 offcore outstanding Demand Data Read requests from SQ that missed L3")

// TODO(dje): LOCK_CYCLES_*
// TODO(dje): IDQ_*

DEF_SKL_EVENT(SKL_ICACHE_64B_IFDATA_STALL,        0x80, 0x04, 0,
  "Cycles stalled due to L1 IC miss")
DEF_SKL_EVENT(SKL_ICACHE_64B_IFTAG_HIT,           0x83, 0x01, 0,
  "IF tag lookups hit in IC")
DEF_SKL_EVENT(SKL_ICACHE_64B_IFTAG_MISS,          0x83, 0x02, 0,
  "IF tag lookups miss in IC")

DEF_SKL_EVENT(SKL_ITLB_MISSES_MISS_CAUSES_A_WALK, 0x85, 0x01, 0,
  "ITLB miss at all levels causing page walk")
DEF_SKL_EVENT(SKL_ITLB_MISSES_WALK_COMPLETED,     0x85, 0x0e, 0,
  "Completed page walks")
DEF_SKL_EVENT(SKL_ITLB_MISSES_WALK_PENDING,       0x85, 0x10, 0,
  "Cycles a PMH is busy with IF page walk")
DEF_SKL_EVENT(SKL_ITLB_MISSES_STLB_HIT,           0x85, 0x20, 0,
  "ITLB misses that hit STLB")

// TODO(dje): ILD_*
// TODO(dje): IDQ_*
// TODO(dje): UOPS_*

DEF_SKL_EVENT(SKL_RESOURCE_STALLS_ANY, 0xa2, 0x01, 0,
  "Resource-related stall cycles")
DEF_SKL_EVENT(SKL_RESOURCE_STALLS_SB, 0xa2, 0x08, 0,
  "Cycles stalled due to no store buffers available (not including draining from sync)")

DEF_SKL_EVENT(SKL_CYCLE_ACTIVITY_CYCLES_L2_MISS, 0xa3, 0x01, IPM_REG_FLAG_CMSK1,
  "Cycles while L2 miss demand load is outstanding")
DEF_SKL_EVENT(SKL_CYCLE_ACTIVITY_CYCLES_L3_MISS, 0xa3, 0x02, IPM_REG_FLAG_CMSK2,
  "Cycles while L3 miss demand load is outstanding")
DEF_SKL_EVENT(SKL_CYCLE_ACTIVITY_STALLS_TOTAL, 0xa3, 0x04, IPM_REG_FLAG_CMSK4,
  "Total execution stalls")
DEF_SKL_EVENT(SKL_CYCLE_ACTIVITY_STALLS_L2_MISS, 0xa3, 0x05, IPM_REG_FLAG_CMSK5,
  "Execution stalls while L2 miss demand load is outstanding")
DEF_SKL_EVENT(SKL_CYCLE_ACTIVITY_STALLS_L3_MISS, 0xa3, 0x06, IPM_REG_FLAG_CMSK6,
  "Execution stalls while L3 miss demand load is outstanding")
DEF_SKL_EVENT(SKL_CYCLE_ACTIVITY_CYCLES_L1D_MISS, 0xa3, 0x08, IPM_REG_FLAG_CMSK8,
  "Cycles while L1 data miss demand load is outstanding")
DEF_SKL_EVENT(SKL_CYCLE_ACTIVITY_STALLS_L1D_MISS, 0xa3, 0x0c, IPM_REG_FLAG_CMSK12,
  "Execution stalls while L1 data miss demand load is outstanding")
DEF_SKL_EVENT(SKL_CYCLE_ACTIVITY_CYCLES_MEM_ANY, 0xa3, 0x10, IPM_REG_FLAG_CMSK16,
  "Cycles while memory subsystem has an outstanding load")
DEF_SKL_EVENT(SKL_CYCLE_ACTIVITY_STALLS_MEM_ANY, 0xa3, 0x14, IPM_REG_FLAG_CMSK20,
  "Execution stalls while memory subsystem has an outstanding load")

// TODO(dje): EXE_ACTIVITY_*
// TODO(dje): LSD_*
// TODO(dje): DSB2MITE_*
// TODO(dje): ITLB_ITLB_FLUSH

DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_DEMAND_DATA_RD, 0xb0, 0x01, 0,
  "Demand Data Read requests sent to uncore")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_DEMAND_CODE_RD, 0xb0, 0x02, 0,
  "Demand Code Read requests sent to uncore")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_DEMAND_RFO, 0xb0, 0x04, 0,
  "Demand RFO read requests sent to uncore, including regular RFOs, locks, ItoM")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_ALL_DATA_RD, 0xb0, 0x08, 0,
  "Data read requests sent to uncore (demand and prefetch)")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_L3_MISS_DEMAND_DATA_RD, 0xb0, 0x10, 0,
  "Demand Data Read requests that missed L3")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_ALL_REQUESTS, 0xb0, 0x80, 0,
  "Any memory transaction that reached the SQ")

// TODO(dje): UOPS_*

DEF_SKL_EVENT(SKL_OFFCORE_REQUEST_BUFFER_SQ_FULL, 0xb2, 0x01, 0,
  "Offcore requests buffer cannot take more entries for this core")

// TODO(dje): OFFCORE_RESPONSE_*
// TODO(dje): TLB_FLUSH_*
// TODO(dje): INST_RETIRED_*
// TODO(dje): OTHER_*
// TODO(dje): UOPS_RETIRED_*
// TODO(dje): MACHINE_CLEARS_*
// TODO(dje): BR_INST_*

DEF_SKL_EVENT(SKL_BR_MISP_RETIRED_ALL_BRANCHES, 0xc5, 0x00, 0,
  "Mispredicted branch instructions at retirement")
DEF_SKL_EVENT(SKL_BR_MISP_RETIRED_CONDITIONAL, 0xc5, 0x01, IPM_REG_FLAG_PS,
  "Mispredicted conditional branch instructions retired")
DEF_SKL_EVENT(SKL_BR_MISP_RETIRED_MACRO, 0xc5, 0x04, IPM_REG_FLAG_PS,
  "Mispredicted macro branch instructions retired")
DEF_SKL_EVENT(SKL_BR_MISP_RETIRED_NEAR_TAKEN, 0xc5, 0x20, IPM_REG_FLAG_PS,
  "Mispredicted near branch instructions retired that were taken")

// TODO(dje): FRONTEND_*
// TODO(dje): FP_ARITH_*
// TODO(dje): FP_ASSIST_ANY

DEF_SKL_EVENT(SKL_HW_INTERRUPTS_RECEIVED, 0xcb, 0x01, 0,
  "Number of h/w interrupts received")

// TODO(dje): MEM_TRANS_*

DEF_SKL_EVENT(SKL_MEM_INST_RETIRED_STLB_MISS_LOADS, 0xd0, 0x11, IPM_REG_FLAG_PSDLA,
  "Retired load instructions that miss the STLB")
DEF_SKL_EVENT(SKL_MEM_INST_RETIRED_STLB_MISS_STORES, 0xd0, 0x12, IPM_REG_FLAG_PSDLA,
  "Retired store instructions that miss the STLB")
DEF_SKL_EVENT(SKL_MEM_INST_RETIRED_LOCKED_LOADS, 0xd0, 0x21, IPM_REG_FLAG_PSDLA,
  "Retired load instructions with locked access")
DEF_SKL_EVENT(SKL_MEM_INST_RETIRED_SPLIT_LOADS, 0xd0, 0x41, IPM_REG_FLAG_PSDLA,
  "Number of load instructions retired with cache-line splits")
DEF_SKL_EVENT(SKL_MEM_INST_RETIRED_SPLIT_STORES, 0xd0, 0x42, IPM_REG_FLAG_PSDLA,
  "Number of store instructions retired with cache-line splits")
DEF_SKL_EVENT(SKL_MEM_INST_RETIRED_ALL_LOADS, 0xd0, 0x81, IPM_REG_FLAG_PSDLA,
  "All retired load instructions")
DEF_SKL_EVENT(SKL_MEM_INST_RETIRED_ALL_STORES, 0xd0, 0x82, IPM_REG_FLAG_PSDLA,
  "All retired store instructions")

DEF_SKL_EVENT(SKL_MEM_LOAD_RETIRED_L1_HIT, 0xd1, 0x01, IPM_REG_FLAG_PSDLA,
  "Retired load instructions with L1 hits as data sources")
DEF_SKL_EVENT(SKL_MEM_LOAD_RETIRED_L2_HIT, 0xd1, 0x02, IPM_REG_FLAG_PSDLA,
  "Retired load instructions with L2 hits as data sources")
DEF_SKL_EVENT(SKL_MEM_LOAD_RETIRED_L3_HIT, 0xd1, 0x04, IPM_REG_FLAG_PSDLA,
  "Retired load instructions with L3 hits as data sources")
DEF_SKL_EVENT(SKL_MEM_LOAD_RETIRED_L1_MISS, 0xd1, 0x08, IPM_REG_FLAG_PSDLA,
  "Retired load instructions missed L1 as data sources")
DEF_SKL_EVENT(SKL_MEM_LOAD_RETIRED_L2_MISS, 0xd1, 0x10, IPM_REG_FLAG_PSDLA,
  "Retired load instructions missed L2, excludes unknown data source")
DEF_SKL_EVENT(SKL_MEM_LOAD_RETIRED_L3_MISS, 0xd1, 0x20, IPM_REG_FLAG_PSDLA,
  "Retired load instructions missed L3, excludes unknown data source")
DEF_SKL_EVENT(SKL_MEM_LOAD_RETIRED_FB_HIT, 0xd1, 0x40, IPM_REG_FLAG_PSDLA,
  "Retired load instructions where data sources were load uops missed L1 but hit FB due to preceding miss to the same cache line with data not ready")

DEF_SKL_EVENT(SKL_MEM_LOAD_L3_HIT_RETIRED_XSNP_MISS, 0xd2, 0x01, IPM_REG_FLAG_PSDLA,
  "Retired load instructions where data sources were L3 hit and cross-core snoop missed in on-pkg core cache")
DEF_SKL_EVENT(SKL_MEM_LOAD_L3_HIT_RETIRED_XSNP_HIT, 0xd2, 0x02, IPM_REG_FLAG_PSDLA,
  "Retired load instructions where data sources were L3 hit and cross-core snoop hits in on-pkg core cache")
DEF_SKL_EVENT(SKL_MEM_LOAD_L3_HIT_RETIRED_XSNP_HITM, 0xd2, 0x04, IPM_REG_FLAG_PSDLA,
  "Retired load instructions where data sources were HitM responses from shared L3")
DEF_SKL_EVENT(SKL_MEM_LOAD_L3_HIT_RETIRED_XSNP_NONE, 0xd2, 0x08, IPM_REG_FLAG_PSDLA,
  "Retired load instructions where data sources were hits in L3 without snoops required")

DEF_SKL_EVENT(SKL_BACLEARS_ANY, 0xe6, 0x01, 0,
  "Number of frontend re-steers due to BPU misprediction")

DEF_SKL_EVENT(SKL_L2_TRANS_L2_WB, 0xf0, 0x40, 0,
  "L2 writebacks that access L2 cache")

DEF_SKL_EVENT(SKL_L2_LINES_IN_ALL, 0xf1, 0x07, 0,
  "L2 cache lines filling L2")

#undef DEF_SKL_EVENT
