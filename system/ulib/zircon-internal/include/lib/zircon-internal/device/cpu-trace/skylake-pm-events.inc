// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Skylake/Kabylake non-architectural performance monitor registers.
// This has the benefit of providing the data in a usage-independent way.

#ifndef DEF_SKL_EVENT
#define DEF_SKL_EVENT(symbol, event_name, id, event, umask, flags, readable_name, description) /* nothing */
#endif

// Non-architectural performance counters
// args:
// - symbol (used in code, must be unique for all symbols within an arch)
//   (must be VALID_C_SYMBOL)
// - event name (used in trace specification files)
//   (must be valid_c_symbol, without the group prefix,
//   i.e., instructions_retired not fixed_instructions_retired)
// - id (10 bits, must be unique for each DEF_SKL_EVENT entry)
//   (N.B. these values are very much not hardwired yet)
// - event (uint32_t)
// - umask (uint32_t)
// - flags (IPM_REG_FLAG_* values) (uint32_t)
// - readable name (ideally max 24 chars for legibility in display in chrome)
// - description (if empty use "")

DEF_SKL_EVENT(SKL_LD_BLOCKS_STORE_FORWARD,
  loads_blocked_by_store,
  0, 0x03, 0x02, 0,
  "Loads blocked by store",   "Loads blocked by overlapping with store buffer that cannot be forwarded")
DEF_SKL_EVENT(SKL_LD_BLOCKS_NO_SR,
  split_loads_blocked,
  1, 0x03, 0x08, 0,
  "Split loads blocked",      "Number of times that split load operations are temporarily blocked because all resources for handling the split are in use")
DEF_SKL_EVENT(SKL_LD_BLOCKS_PARTIAL_ADDRESS_ALIAS,
  false_mob_dependencies,
  2, 0x07, 0x01, 0,
  "False MOB dependencies",   "False dependencies in MOB due to partial compare on address")

DEF_SKL_EVENT(SKL_DTLB_LOAD_MISSES_MISS_CAUSES_A_WALK,
  load_misses_page_walk,
  10, 0x08, 0x01, 0,
  "Load misses, page walk",   "Load misses in all TLB levels that cause a page walk")
DEF_SKL_EVENT(SKL_DTLB_LOAD_MISSES_WALK_COMPLETED,
  load_misses_walk_completes,
  11, 0x08, 0x0e, 0,
  "Load misses, walk completes", "Load misses in all TBL levels causes a page walk that completes")
DEF_SKL_EVENT(SKL_DTLB_LOAD_MISSES_WALK_PENDING,
  load_misses_walk_pending,
  12, 0x08, 0x10, 0,
  "Load misses, walk pending", "Counts 1 per cycle for each PMH that is busy with a page walk for a load")
DEF_SKL_EVENT(SKL_DTLB_LOAD_MISSES_WALK_ACTIVE,
  load_misses_walk_active,
  13, 0x08, 0x10, IPM_REG_FLAG_CMSK1,
  "Load misses, walk active", "Cycles when at least one PMH is busy with a page walk for a load")
DEF_SKL_EVENT(SKL_DTLB_LOAD_MISSES_STLB_HIT,
  load_misses_stlb_hit,
  14, 0x08, 0x20, 0,
  "Load misses, STLB hit",    "Loads that miss the DTBL but hit STLB")

DEF_SKL_EVENT(SKL_INT_MISC_RECOVERY_CYCLES,
  misc_recovery_cycles,
  20, 0x0d, 0x01, 0,
  "Misc recovery cycles",     "Core cycles the allocator was stalled due to recovery from earlier machine clear event for this thread")
DEF_SKL_EVENT(SKL_INT_MISC_RECOVERY_CYCLES_ANY,
  misc_recovery_cycles_any,
  21, 0x0d, 0x01, IPM_REG_FLAG_ANYT,
  "Misc recovery cycles (any)", "Core cycles the allocator was stalled due to recovery from earlier machine clear event for any logical thread on this processor core")
DEF_SKL_EVENT(SKL_INT_MISC_CLEAR_RESTEER_CYCLES,
  misc_clear_resteer_cycles,
  22, 0x0d, 0x80, 0,
  "Misc clear resteer cycles", "Cycles the issue-stage is waiting for front end to fetch from re-steered path following branch misprediction or machine clear events")

// TODO(dje): UOPS_*
// TODO(dje): ARITH_*

DEF_SKL_EVENT(SKL_L2_RQSTS_DEMAND_DATA_RD_MISS,
  ddr_l2_misses,
  30, 0x24, 0x21, 0,
  "DDR L2 misses",            "Demand Data Read requests that missed L2, no rejects")
DEF_SKL_EVENT(SKL_L2_RQSTS_RFO_MISS,
  rfo_rqsts_l2_misses,
  31, 0x24, 0x22, 0,
  "RFO rqsts L2 misses",      "RFO requests that missed L2")
DEF_SKL_EVENT(SKL_L2_RQSTS_CODE_RD_MISS,
  ifetch_l2_misses,
  32, 0x24, 0x24, 0,
  "Ifetch L2 misses",         "L2 misses when fetching instructions")
DEF_SKL_EVENT(SKL_L2_RQSTS_ALL_DEMAND_MISS,
  all_demand_rqsts_l2_misses,
  33, 0x24, 0x27, 0,
  "All demand rqsts L2 misses", "Demand requests that missed L2")
DEF_SKL_EVENT(SKL_L2_RQSTS_PF_MISS,
  prefetch_l2_misses,
  34, 0x24, 0x38, 0,
  "Prefetch L2 misses",       "Requests from L1/L2/L3 h/w prefetchers or load s/w prefetches that miss L2")
DEF_SKL_EVENT(SKL_L2_RQSTS_MISS,
  all_l2_misses,
  35, 0x24, 0x3f, 0,
  "All L2 misses",            "All requests that missed L2")
DEF_SKL_EVENT(SKL_L2_RQSTS_DEMAND_DATA_RD_HIT,
  ddr_l2_hits,
  36, 0x24, 0x41, 0,
  "DDR L2 hits",              "Demand Data Read requests that hit L2")
DEF_SKL_EVENT(SKL_L2_RQSTS_RFO_HIT,
  rfo_rqsts_l2_hits,
  37, 0x24, 0x42, 0,
  "RFO rqsts L2 hits",        "RFO requests that hit L2")
DEF_SKL_EVENT(SKL_L2_RQSTS_CODE_RD_HIT,
  ifetch_l2_hits,
  38, 0x24, 0x44, 0,
  "Ifetch L2 hits",           "L2 hits when fetching instructions")
DEF_SKL_EVENT(SKL_L2_RQSTS_PF_HIT,
  prefetch_l2_hits,
  39, 0x24, 0xd8, 0,
  "Prefetch L2 hits",         "Prefetches that hit L2")
DEF_SKL_EVENT(SKL_L2_RQSTS_ALL_DEMAND_DATA_RD,
  all_l2_ddr_rqsts,
  40, 0x24, 0xe1, 0,
  "All L2 DDR rqsts",         "All Demand Data Read requests to L2")
DEF_SKL_EVENT(SKL_L2_RQSTS_ALL_RFO,
  all_l2_rfo_rqsts,
  41, 0x24, 0xe2, 0,
  "All L2 RFO rqsts",         "All RFO requests to L2")
DEF_SKL_EVENT(SKL_L2_RQSTS_ALL_CODE_RD,
  all_l2_code_rqsts,
  42, 0x24, 0xe4, 0,
  "All L2 code rqsts",        "All code requests to L2")
DEF_SKL_EVENT(SKL_L2_RQSTS_ALL_DEMAND_REFERENCES,
  all_l2_demand_rqsts,
  43, 0x24, 0xe7, 0,
  "All L2 demand rqsts",      "All demand requests to L2")
DEF_SKL_EVENT(SKL_L2_RQSTS_ALL_PF,
  all_l2_prefetch_rqsts,
  44, 0x24, 0xf8, 0,
  "All L2 prefetch rqsts",    "All requests from L1/L2/L3 h/w prefetchers or load s/w prefetches")
DEF_SKL_EVENT(SKL_L2_RQSTS_REFERENCES,
  all_l2_rqsts,
  45, 0x24, 0xef, 0,
  "All L2 rqsts",             "All requests to L2")

DEF_SKL_EVENT(SKL_LONGEST_LAT_CACHE_REFERENCE,
  llc_cache_reference,
  50, 0x2e, 0x4f, 0,
  "LLC cache reference",      "Requests originating from core that reference cache line in L3")
DEF_SKL_EVENT(SKL_LONGEST_LAT_CACHE_MISS,
  llc_cache_miss,
  51, 0x2e, 0x41, 0,
  "LLC cache miss",           "Cache miss condition for references to L3")

// TODO(dje): CPU_CLK_*

DEF_SKL_EVENT(SKL_L1D_PEND_MISS_PENDING,
  l1d_misses_per_cycle,
  60, 0x48, 0x01, 0,
  "L1D misses per cycle",     "Number of outstanding L1D misses every cycle")
DEF_SKL_EVENT(SKL_L1D_PEND_MISS_PENDING_CYCLES,
  l1d_misses_per_cycle_this,
  61, 0x48, 0x01, IPM_REG_FLAG_CMSK1,
  "L1D misses per cycle (this)", "Number of outstanding L1D misses for this logical processor")
DEF_SKL_EVENT(SKL_L1D_PEND_MISS_PENDING_CYCLES_ANY,
  l1d_misses_per_cycle_any,
  62, 0x48, 0x01, IPM_REG_FLAG_CMSK1 | IPM_REG_FLAG_ANYT,
  "L1D misses per cycle (any)", "Number of outstanding L1D misses for any logical thread on this processor core")
DEF_SKL_EVENT(SKL_L1D_PEND_MISS_FB_FULL,
  l1d_miss_fb_full,
  63, 0x48, 0x02, 0,
  "L1D miss, FB full",        "Number of times a request needed an FB entry but no entry was available")

DEF_SKL_EVENT(SKL_DTLB_STORE_MISSES_MISS_CAUSES_A_WALK,
  store_miss_page_walk,
  70, 0x49, 0x01, 0,
  "Store miss, page walk",      "Store misses in all TBL levels that cause page walks")
DEF_SKL_EVENT(SKL_DTLB_STORE_MISSES_WALK_COMPLETED,
  store_miss_completed_walk,
  71, 0x49, 0x0e, 0,
  "Store miss, completed walk", "Completed page walks in any TLB levels due to store misses")
DEF_SKL_EVENT(SKL_DTLB_STORE_MISSES_WALK_PENDING,
  store_miss_walk_pending,
  72, 0x49, 0x10, 0,
  "Store miss, walk pending", "Counts 1 per cycle for each PMH that is busy with a page walk for a store")
DEF_SKL_EVENT(SKL_DTLB_STORE_MISSES_WALK_ACTIVE,
  store_miss_walk_active,
  73, 0x49, 0x10, IPM_REG_FLAG_CMSK1,
  "Store miss, walk active",  "Cycles when at least one PMH is busy with a page walk for a store")
DEF_SKL_EVENT(SKL_DTLB_STORE_MISSES_STLB_HIT,
  store_miss_stlb_hit,
  74, 0x49, 0x20, 0,
  "Store miss, STLB hit",     "Store misses that missed DTLB but hit STLB")

// TODO(dje): LOAD_HIT_PRE_HW_PF, EPT_WALK_PENDING

DEF_SKL_EVENT(SKL_L1D_REPLACEMENT,
  num_lines_to_l1_data,
  80, 0x51, 0x01, 0,
  "#lines -> L1 data",       "Number of lines brought into L1 data cache")

// TODO(dje): RS_*

DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_OUTSTANDING_DEMAND_DATA_RD,
  offcore_ddr,
  90, 0x60, 0x01, 0,
  "Offcore DDR",             "Incremented each cycle of the number of offcore outstanding Demand Data Read transactions in SQ to uncore")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_OUTSTANDING_CYCLES_WITH_DEMAND_DATA_RD,
  offcore_1_ddr,
  91, 0x60, 0x01, IPM_REG_FLAG_CMSK1,
  "Offcore 1 DDR",           "Cycles with at least 1 offcore outstanding Demand Data Read transactions in SQ to uncore")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_OUTSTANDING_CYCLES_WITH_DEMAND_DATA_RD_GE_6,
  offcore_6_ddr,
  92, 0x60, 0x01, IPM_REG_FLAG_CMSK6,
  "Offcore 6 DDR",           "Cycles with at least 6 offcore outstanding Demand Data Read transactions in SQ to uncore")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_OUTSTANDING_DEMAND_CODE_RD,
  offcore_dcr,
  93, 0x60, 0x02, 0,
  "Offcore DCR",             "Incremented each cycle of the number of offcore outstanding Demand Code Read transactions in SQ to uncore") 
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_OUTSTANDING_CYCLES_WITH_DEMAND_CODE_RD,
  offcore_1_dcr,
  94, 0x60, 0x02, IPM_REG_FLAG_CMSK1,
  "Offcore 1 DCR",           "Cycles with at least 1 offcore outstanding Demand Code Read transactions in SQ to uncore")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_OUTSTANDING_DEMAND_RFO,
  offcore_rfo,
  95, 0x60, 0x04, 0,
  "Offcore RFO",             "Incremented each cycle of the number of offcore outstanding RFO store transactions in SQ to uncore")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_OUTSTANDING_CYCLES_WITH_DEMAND_RFO,
  offcore_1_rfo,
  96, 0x60, 0x04, IPM_REG_FLAG_CMSK1,
  "Offcore 1 RFO",           "Cycles with at least 1 offcore outstanding RFO transactions in SQ to uncore")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_OUTSTANDING_DATA_RD,
  offcore_data_read,
  97, 0x60, 0x08, 0,
  "Offcore data read",       "Incremented each cycle of the number of offcore outstanding data read transactions in SQ to uncore")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_OUTSTANDING_CYCLES_WITH_DATA_RD,
  offcore_1_data_read,
  98, 0x60, 0x08, IPM_REG_FLAG_CMSK1,
  "Offcore 1 data read",     "Cycles with at least 1 offcore outstanding data read transactions in SQ to uncore")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_OUTSTANDING_L3_MISS_DEMAND_DATA_RD,
  offcore_ddr_l3_miss,
  99, 0x60, 0x10, 0,
  "Offcore DDR, L3 miss",    "Incremented each cycle of the number of offcore outstanding Demand Data Read requests from SQ that missed L3")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_OUTSTANDING_CYCLES_WITH_L3_MISS_DEMAND_DATA_RD,
  offcore_1_ddr_l3_miss,
  100, 0x60, 0x10, IPM_REG_FLAG_CMSK1,
  "Offcore 1 DDR, L3 miss",  "Cycles with at least 1 offcore outstanding Demand Data Read request from SQ that missed L3")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_OUTSTANDING_CYCLES_WITH_L3_MISS_DEMAND_DATA_RD_GE_6,
  offcore_6_ddr_l3_miss,
  101, 0x60, 0x10, IPM_REG_FLAG_CMSK6,
  "Offcore 6 DDR, L3 miss",  "Cycles with at least 6 offcore outstanding Demand Data Read requests from SQ that missed L3")

// TODO(dje): LOCK_CYCLES_*
// TODO(dje): IDQ_*

DEF_SKL_EVENT(SKL_ICACHE_64B_IFDATA_STALL,
  cycles_stalled_l1_ic_miss,
  110, 0x80, 0x04, 0,
  "Cycles stalled, L1 IC miss", "Cycles stalled due to L1 IC miss")
DEF_SKL_EVENT(SKL_ICACHE_64B_IFTAG_HIT,
  if_tag_lookups_ic_hit,
  111, 0x83, 0x01, 0,
  "IF tag lookups, IC hit",     "IF tag lookups hit in IC")
DEF_SKL_EVENT(SKL_ICACHE_64B_IFTAG_MISS,
  if_tag_lookups_ic_miss,
  112, 0x83, 0x02, 0,
  "IF tag lookups, IC miss",    "IF tag lookups miss in IC")

DEF_SKL_EVENT(SKL_ITLB_MISSES_MISS_CAUSES_A_WALK,
  itlb_miss_page_walk,
  120, 0x85, 0x01, 0,
  "ITLB miss, page walk",       "ITLB miss at all levels causing page walk")
DEF_SKL_EVENT(SKL_ITLB_MISSES_WALK_COMPLETED,
  itlb_miss_walk_completed,
  121, 0x85, 0x0e, 0,
  "ITLB miss, walk completed",  "ITLB completed page walks")
DEF_SKL_EVENT(SKL_ITLB_MISSES_WALK_PENDING,
  pmh_busy_if_page_walk,
  122, 0x85, 0x10, 0,
  "PMH busy, IF page walk",     "Cycles a PMH is busy with IF page walk")
DEF_SKL_EVENT(SKL_ITLB_MISSES_STLB_HIT,
  itbl_miss_stlb_hit,
  123, 0x85, 0x20, 0,
  "ITLB miss, STLB hit",        "ITLB misses that hit STLB")

// TODO(dje): ILD_*
// TODO(dje): IDQ_*
// TODO(dje): UOPS_*

DEF_SKL_EVENT(SKL_RESOURCE_STALLS_ANY,
  resource_stalls,
  130, 0xa2, 0x01, 0,
  "Resource stalls",          "Resource-related stall cycles")
DEF_SKL_EVENT(SKL_RESOURCE_STALLS_SB,
  sb_full_stalls,
  131, 0xa2, 0x08, 0,
  "SB full stalls",           "Cycles stalled due to no store buffers available (not including draining from sync)")

DEF_SKL_EVENT(SKL_CYCLE_ACTIVITY_CYCLES_L2_MISS,
  l2_miss_demand_load_outstanding,
  140, 0xa3, 0x01, IPM_REG_FLAG_CMSK1,
  "L2 miss, demand load outstanding", "Cycles while L2 miss demand load is outstanding")
DEF_SKL_EVENT(SKL_CYCLE_ACTIVITY_CYCLES_L3_MISS,
  l3_miss_demand_load_outstanding,
  141, 0xa3, 0x02, IPM_REG_FLAG_CMSK2,
  "L3 miss, demand load outstanding", "Cycles while L3 miss demand load is outstanding")
DEF_SKL_EVENT(SKL_CYCLE_ACTIVITY_STALLS_TOTAL,
  total_execution_stalls,
  142, 0xa3, 0x04, IPM_REG_FLAG_CMSK4,
  "Total execution stalls",           "")
DEF_SKL_EVENT(SKL_CYCLE_ACTIVITY_STALLS_L2_MISS,
  l2_miss_execution_stalls,
  143, 0xa3, 0x05, IPM_REG_FLAG_CMSK5,
  "L2 miss, execution stalls",        "Execution stalls while L2 miss demand load is outstanding")
DEF_SKL_EVENT(SKL_CYCLE_ACTIVITY_STALLS_L3_MISS,
  l3_miss_execution_stalls,
  144, 0xa3, 0x06, IPM_REG_FLAG_CMSK6,
  "L3 miss, execution stalls",        "Execution stalls while L3 miss demand load is outstanding")
DEF_SKL_EVENT(SKL_CYCLE_ACTIVITY_CYCLES_L1D_MISS,
  l1_miss_demand_load_outstanding,
  145, 0xa3, 0x08, IPM_REG_FLAG_CMSK8,
  "L1 miss, demand load outstanding", "Cycles while L1 data miss demand load is outstanding")
DEF_SKL_EVENT(SKL_CYCLE_ACTIVITY_STALLS_L1D_MISS,
  l1_miss_execution_stalls,
  146, 0xa3, 0x0c, IPM_REG_FLAG_CMSK12,
  "L1 miss, execution stalls",        "Execution stalls while L1 data miss demand load is outstanding")
DEF_SKL_EVENT(SKL_CYCLE_ACTIVITY_CYCLES_MEM_ANY,
  mem_subsystem_outstanding_load,
  147, 0xa3, 0x10, IPM_REG_FLAG_CMSK16,
  "Mem subsystem outstanding load",   "Cycles while memory subsystem has an outstanding load")
DEF_SKL_EVENT(SKL_CYCLE_ACTIVITY_STALLS_MEM_ANY,
  mem_subsystem_execution_stalls,
  148, 0xa3, 0x14, IPM_REG_FLAG_CMSK20,
  "Mem subsystem execution stalls",   "Execution stalls while memory subsystem has an outstanding load")

// TODO(dje): EXE_ACTIVITY_*
// TODO(dje): LSD_*
// TODO(dje): DSB2MITE_*
// TODO(dje): ITLB_ITLB_FLUSH

DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_DEMAND_DATA_RD,
  ddr_rqsts_to_uncore,
  150, 0xb0, 0x01, 0,
  "DDR rqsts to uncore",      "Demand Data Read requests sent to uncore")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_DEMAND_CODE_RD,
  dcr_rqsts_to_uncore,
  151, 0xb0, 0x02, 0,
  "DCR rqsts to uncore",      "Demand Code Read requests sent to uncore")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_DEMAND_RFO,
  rfo_rqsts_to_uncore,
  152, 0xb0, 0x04, 0,
  "RFO rqsts to uncore",      "Demand RFO read requests sent to uncore, including regular RFOs, locks, ItoM")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_ALL_DATA_RD,
  data_rd_rqsts_to_uncore,
  153, 0xb0, 0x08, 0,
  "Data rd rqsts to uncore",  "Data read requests sent to uncore (demand and prefetch)")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_L3_MISS_DEMAND_DATA_RD,
  ddr_rqsts_missed_l3,
  154, 0xb0, 0x10, 0,
  "DDR rqsts missed L3",      "Demand Data Read requests that missed L3")
DEF_SKL_EVENT(SKL_OFFCORE_REQUESTS_ALL_REQUESTS,
  any_mem_rqsts_reaching_sq,
  155, 0xb0, 0x80, 0,
  "Any mem rqsts reaching SQ", "Any memory transaction that reached the SQ")

// TODO(dje): UOPS_*

DEF_SKL_EVENT(SKL_OFFCORE_REQUEST_BUFFER_SQ_FULL,
  offcore_rqst_buffer_full,
  160, 0xb2, 0x01, 0,
  "Offcore rqst buffer full", "Offcore requests buffer cannot take more entries for this core")

// TODO(dje): OFFCORE_RESPONSE_*
// TODO(dje): TLB_FLUSH_*
// TODO(dje): INST_RETIRED_*
// TODO(dje): OTHER_*
// TODO(dje): UOPS_RETIRED_*
// TODO(dje): MACHINE_CLEARS_*
// TODO(dje): BR_INST_*

DEF_SKL_EVENT(SKL_BR_MISP_RETIRED_ALL_BRANCHES,
  mispred_branch_insns,
  170, 0xc5, 0x00, 0,
  "Mispred branch insns",       "Mispredicted branch instructions at retirement")
DEF_SKL_EVENT(SKL_BR_MISP_RETIRED_CONDITIONAL,
  mispred_cbranch_insns,
  171, 0xc5, 0x01, IPM_REG_FLAG_PS,
  "Mispred cbranch insns",      "Mispredicted conditional branch instructions retired")
DEF_SKL_EVENT(SKL_BR_MISP_RETIRED_MACRO,
  mispred_macro_branch_insns,
  172, 0xc5, 0x04, IPM_REG_FLAG_PS,
  "Mispred macro branch insns", "Mispredicted macro branch instructions retired")
DEF_SKL_EVENT(SKL_BR_MISP_RETIRED_NEAR_TAKEN,
  mispred_near_branch_taken,
  173, 0xc5, 0x20, IPM_REG_FLAG_PS,
  "Mispred near branch, taken", "Mispredicted near branch instructions retired that were taken")

// TODO(dje): FRONTEND_*
// TODO(dje): FP_ARITH_*
// TODO(dje): FP_ASSIST_ANY

DEF_SKL_EVENT(SKL_HW_INTERRUPTS_RECEIVED,
  number_hw_interrupts,
  180, 0xcb, 0x01, 0,
  "Number h/w interrupts",    "Number of h/w interrupts received")

// TODO(dje): MEM_TRANS_*

DEF_SKL_EVENT(SKL_MEM_INST_RETIRED_STLB_MISS_LOADS,
  retired_load_miss_stlb,
  190, 0xd0, 0x11, IPM_REG_FLAG_PSDLA,
  "Retired load, miss STLB",  "Retired load instructions that miss the STLB")
DEF_SKL_EVENT(SKL_MEM_INST_RETIRED_STLB_MISS_STORES,
  retired_store_miss_stlb,
  191, 0xd0, 0x12, IPM_REG_FLAG_PSDLA,
  "Retired store, miss STLB", "Retired store instructions that miss the STLB")
DEF_SKL_EVENT(SKL_MEM_INST_RETIRED_LOCKED_LOADS,
  retired_load_locked,
  192, 0xd0, 0x21, IPM_REG_FLAG_PSDLA,
  "Retired load, locked",     "Retired load instructions with locked access")
DEF_SKL_EVENT(SKL_MEM_INST_RETIRED_SPLIT_LOADS,
  retired_load_line_split,
  193, 0xd0, 0x41, IPM_REG_FLAG_PSDLA,
  "Retired load, line split", "Number of load instructions retired with cache-line splits")
DEF_SKL_EVENT(SKL_MEM_INST_RETIRED_SPLIT_STORES,
  retired_store_line_split,
  194, 0xd0, 0x42, IPM_REG_FLAG_PSDLA,
  "Retired store, line split", "Number of store instructions retired with cache-line splits")
DEF_SKL_EVENT(SKL_MEM_INST_RETIRED_ALL_LOADS,
  all_retired_load_insns,
  195, 0xd0, 0x81, IPM_REG_FLAG_PSDLA,
  "All retired load insns",   "All retired load instructions")
DEF_SKL_EVENT(SKL_MEM_INST_RETIRED_ALL_STORES,
  all_retired_store_insns,
  196, 0xd0, 0x82, IPM_REG_FLAG_PSDLA,
  "All retired store insns",  "All retired store instructions")

DEF_SKL_EVENT(SKL_MEM_LOAD_RETIRED_L1_HIT,
  retired_load_l1_hit,
  200, 0xd1, 0x01, IPM_REG_FLAG_PSDLA,
  "Retired load, L1 hit",     "Retired load instructions with L1 hits as data sources")
DEF_SKL_EVENT(SKL_MEM_LOAD_RETIRED_L2_HIT,
  retired_load_l2_hit,
  201, 0xd1, 0x02, IPM_REG_FLAG_PSDLA,
  "Retired load, L2 hit",     "Retired load instructions with L2 hits as data sources")
DEF_SKL_EVENT(SKL_MEM_LOAD_RETIRED_L3_HIT,
  retired_load_l3_hit,
  202, 0xd1, 0x04, IPM_REG_FLAG_PSDLA,
  "Retired load, L3 hit",     "Retired load instructions with L3 hits as data sources")
DEF_SKL_EVENT(SKL_MEM_LOAD_RETIRED_L1_MISS,
  retired_load_l1_miss,
  203, 0xd1, 0x08, IPM_REG_FLAG_PSDLA,
  "Retired load, L1 miss",    "Retired load instructions missed L1 as data sources")
DEF_SKL_EVENT(SKL_MEM_LOAD_RETIRED_L2_MISS,
  retired_load_l2_miss,
  204, 0xd1, 0x10, IPM_REG_FLAG_PSDLA,
  "Retired load, L2 miss",    "Retired load instructions missed L2, excludes unknown data source")
DEF_SKL_EVENT(SKL_MEM_LOAD_RETIRED_L3_MISS,
  retired_load_l3_miss,
  205, 0xd1, 0x20, IPM_REG_FLAG_PSDLA,
  "Retired load, L3 miss",    "Retired load instructions missed L3, excludes unknown data source")
DEF_SKL_EVENT(SKL_MEM_LOAD_RETIRED_FB_HIT,
  retired_load_fb_hit,
  206, 0xd1, 0x40, IPM_REG_FLAG_PSDLA,
  "Retired load, FB hit",     "Retired load instructions where data sources were load uops missed L1 but hit FB due to preceding miss to the same cache line with data not ready")

DEF_SKL_EVENT(SKL_MEM_LOAD_L3_HIT_RETIRED_XSNP_MISS,
  retired_load_l3_hit_cross_core_snoop_miss,
  210, 0xd2, 0x01, IPM_REG_FLAG_PSDLA,
  "Retired Load, L3 hit, cross-core snoop miss", "Retired load instructions where data sources were L3 hit and cross-core snoop missed in on-pkg core cache")
DEF_SKL_EVENT(SKL_MEM_LOAD_L3_HIT_RETIRED_XSNP_HIT,
  retired_load_l3_hit_cross_core_snoop_hit,
  211, 0xd2, 0x02, IPM_REG_FLAG_PSDLA,
  "Retired load, L3 hit, cross-core snoop hit", "Retired load instructions where data sources were L3 hit and cross-core snoop hits in on-pkg core cache")
DEF_SKL_EVENT(SKL_MEM_LOAD_L3_HIT_RETIRED_XSNP_HITM,
  retired_load_l3_hitm,
  212, 0xd2, 0x04, IPM_REG_FLAG_PSDLA,
  "Retired load, L3 hitm",    "Retired load instructions where data sources were HitM responses from shared L3")
DEF_SKL_EVENT(SKL_MEM_LOAD_L3_HIT_RETIRED_XSNP_NONE,
  retired_load_l3_hit_no_snoop,
  213, 0xd2, 0x08, IPM_REG_FLAG_PSDLA,
  "Retired load, L3 hit, no snoop", "Retired load instructions where data sources were hits in L3 without snoops required")

DEF_SKL_EVENT(SKL_BACLEARS_ANY,
  frontend_resteer_bpu_mispred,
  220, 0xe6, 0x01, 0,
  "Frontend resteer, BPU mispred", "Number of frontend re-steers due to BPU misprediction")

DEF_SKL_EVENT(SKL_L2_TRANS_L2_WB,
  l2_writebacks,
  230, 0xf0, 0x40, 0,
  "L2 writebacks",            "L2 writebacks that access L2 cache")

DEF_SKL_EVENT(SKL_L2_LINES_IN_ALL,
  l2_fills,
  240, 0xf1, 0x07, 0,
  "L2 fills",                 "L2 cache lines filling L2")

#undef DEF_SKL_EVENT
