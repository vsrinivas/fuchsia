// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "arch/arm64/registers.h"

#include <arch/arm64.h>
#include <kernel/thread.h>
#include <vm/vm.h>

void arm64_set_debug_state_for_thread(thread_t* thread, bool active) {
  arm64_iframe_t* iframe = thread->arch.suspended_general_regs;
  DEBUG_ASSERT(iframe);
  if (active) {
    iframe->mdscr |= ARM64_MDSCR_EL1_MDE;  // MDE enables hardware exceptions.
    iframe->mdscr |= ARM64_MDSCR_EL1_KDE;  // KDE enables local debugging in EL0.
  } else {
    iframe->mdscr &= ~ARM64_MDSCR_EL1_MDE;  // MDE enables hardware exceptions.
    iframe->mdscr &= ~ARM64_MDSCR_EL1_KDE;  // KDE enables local debugging in EL0.
  }
}

void arm64_set_debug_state_for_cpu(bool active) {
  uint32_t mdscr = __arm_rsr("mdscr_el1");
  if (active) {
    mdscr |= ARM64_MDSCR_EL1_MDE;  // MDE enables hardware exceptions.
    mdscr |= ARM64_MDSCR_EL1_KDE;  // KDE enables local debugging in EL0.
  } else {
    mdscr &= ~ARM64_MDSCR_EL1_MDE;  // MDE enables hardware exceptions.
    mdscr &= ~ARM64_MDSCR_EL1_KDE;  // KDE enables local debugging in EL0.
  }
  __arm_wsr("mdscr_el1", mdscr);
  __isb(ARM_MB_SY);
}

static bool arm64_validate_hw_breakpoints(arm64_debug_state_t* state,
                                          uint32_t* active_breakpoints) {
  uint32_t breakpoint_count = 0;

  // Validate that the addresses are valid. HW breakpoints that are beyond the
  // arch resource count will be ignored. This is because zircon will never
  // update or use those values in any way, so they will always be zero,
  // independent of what the user provided within those.
  size_t hw_bp_count = arm64_hw_breakpoint_count();
  for (size_t i = 0; i < ARM64_MAX_HW_BREAKPOINTS; i++) {
    uint32_t dbgbcr = state->hw_bps[i].dbgbcr;
    uint64_t dbgbvr = state->hw_bps[i].dbgbvr;

    // Breakpoints values beyond the CPU count won't ever be written to the
    // debug state, so they can be ignored.
    if (i >= hw_bp_count) {
      break;
    }

    // Verify that the breakpoint refers to userspace.
    if (dbgbvr != 0 && !is_user_address(dbgbvr)) {
      return false;
    }
    state->hw_bps[i].dbgbvr &= ARM64_DBGBVR_USER_MASK;

    // If the address is valid and the breakpoint is activated, we mask in
    // the other necessary value and count it for bookkeepping.
    if (dbgbcr & ARM64_DBGBCR_E_MASK) {
      state->hw_bps[i].dbgbcr = ARM64_DBGBCR_ACTIVE_MASK;
      breakpoint_count++;
    } else {
      state->hw_bps[i].dbgbcr = 0;
    }
  }

  *active_breakpoints = breakpoint_count;
  return true;
}

static bool arm64_validate_hw_watchpoints(arm64_debug_state_t* state,
                                          uint32_t* active_watchpoints) {
  uint32_t watchpoint_count = 0;
  // Validate that the addresses are valid. HW watchpoint that are beyond the
  // arch resource count will be ignored. This is because zircon will never
  // update or use those values in any way, so they will always be zero,
  // independent of what the user provided within those.
  size_t hw_wp_count = arm64_hw_watchpoint_count();
  for (size_t i = 0; i < hw_wp_count; i++) {
    uint32_t& dbgwcr = state->hw_wps[i].dbgwcr;
    uint64_t& dbgwvr = state->hw_wps[i].dbgwvr;

    // Watchpoints values beyond the CPU count won't ever be written to the
    // debug state, so they can be ignored.
    if (i >= hw_wp_count) {
      break;
    }

    // Verify that the breakpoint refers to userspace.
    if (dbgwvr != 0 && !is_user_address(dbgwvr)) {
      return false;
    }
    dbgwvr &= ARM64_DBGWVR_USER_MASK;

    // If the address is valid and the watchpoint is active, we mask in
    // the other necessary bits.
    if (ARM64_DBGWCR_E_GET(dbgwcr)) {
      dbgwcr &= ARM64_DBGWCR_ACTIVE_MASK;

      // See zircon/hw/debug/arm64.h for details on the fields set for PAC, HMC and SSC.
      ARM64_DBGWCR_PAC_SET(&dbgwcr, 0b10);
      ARM64_DBGWCR_SSC_SET(&dbgwcr, 0b01);

      // TODO(donosoc): Expose this field to userspace.
      ARM64_DBGWCR_LSC_SET(&dbgwcr, 0b10);

      watchpoint_count++;
    }
  }

  *active_watchpoints = watchpoint_count;
  return true;
}

bool arm64_validate_debug_state(arm64_debug_state_t* state, uint32_t* active_breakpoints,
                                uint32_t* active_watchpoints) {
  if (!arm64_validate_hw_breakpoints(state, active_breakpoints) ||
      !arm64_validate_hw_watchpoints(state, active_watchpoints)) {
    return false;
  }
  return true;
}

uint8_t arm64_hw_breakpoint_count() {
  uint64_t dfr0 = __arm_rsr64("id_aa64dfr0_el1");
  uint8_t count =
      (uint8_t)(((dfr0 & ARM64_ID_AADFR0_EL1_BRPS) >> ARM64_ID_AADFR0_EL1_BRPS_SHIFT) + 1lu);
  // ARMv8 assures at least 2 hw registers.
  DEBUG_ASSERT(count >= ARM64_MIN_HW_BREAKPOINTS && count <= ARM64_MAX_HW_BREAKPOINTS);
  return count;
}

uint8_t arm64_hw_watchpoint_count() {
  uint64_t dfr0 = __arm_rsr64("id_aa64dfr0_el1");
  uint8_t count =
      (uint8_t)(((dfr0 & ARM64_ID_AADFR0_EL1_WRPS) >> ARM64_ID_AADFR0_EL1_WRPS_SHIFT) + 1lu);
  // ARMv8 assures at least 2 hw registers.
  DEBUG_ASSERT(count >= ARM64_MIN_HW_WATCHPOINTS && count <= ARM64_MAX_HW_WATCHPOINTS);
  return count;
}

// Read Debug State ------------------------------------------------------------------------------

#define READ_HW_BREAKPOINT(index, dbgbcr_val, dbgbvr_val) \
  dbgbcr_val = __arm_rsr("dbgbcr" #index "_el1");         \
  dbgbvr_val = __arm_rsr64("dbgbvr" #index "_el1");
#define READ_HW_BREAKPOINT_CASE(index, dbgbcr_val, dbgbvr_val) \
  case index:                                                  \
    READ_HW_BREAKPOINT(index, dbgbcr_val, dbgbvr_val);         \
    break;

static void arm64_read_hw_breakpoint_by_index(unsigned int index, uint32_t* dbgbcr,
                                              uint64_t* dbgbvr) {
  uint32_t read_dbgbcr = 0;
  uint64_t read_dbgbvr = 0;
  switch (index) {
    READ_HW_BREAKPOINT_CASE(0, read_dbgbcr, read_dbgbvr);
    READ_HW_BREAKPOINT_CASE(1, read_dbgbcr, read_dbgbvr);
    READ_HW_BREAKPOINT_CASE(2, read_dbgbcr, read_dbgbvr);
    READ_HW_BREAKPOINT_CASE(3, read_dbgbcr, read_dbgbvr);
    READ_HW_BREAKPOINT_CASE(4, read_dbgbcr, read_dbgbvr);
    READ_HW_BREAKPOINT_CASE(5, read_dbgbcr, read_dbgbvr);
    READ_HW_BREAKPOINT_CASE(6, read_dbgbcr, read_dbgbvr);
    READ_HW_BREAKPOINT_CASE(7, read_dbgbcr, read_dbgbvr);
    READ_HW_BREAKPOINT_CASE(8, read_dbgbcr, read_dbgbvr);
    READ_HW_BREAKPOINT_CASE(9, read_dbgbcr, read_dbgbvr);
    READ_HW_BREAKPOINT_CASE(10, read_dbgbcr, read_dbgbvr);
    READ_HW_BREAKPOINT_CASE(11, read_dbgbcr, read_dbgbvr);
    READ_HW_BREAKPOINT_CASE(12, read_dbgbcr, read_dbgbvr);
    READ_HW_BREAKPOINT_CASE(13, read_dbgbcr, read_dbgbvr);
    READ_HW_BREAKPOINT_CASE(14, read_dbgbcr, read_dbgbvr);
    READ_HW_BREAKPOINT_CASE(15, read_dbgbcr, read_dbgbvr);
    default:
      DEBUG_ASSERT(false);
  }

  *dbgbcr = read_dbgbcr;
  *dbgbvr = read_dbgbvr;
}

#undef READ_HW_BREAKPOINT
#undef READ_HW_BREAKPOINT_CASE

void arm64_read_hw_debug_regs(arm64_debug_state_t* debug_state) {
  // We clear the state out.
  *debug_state = {};

  // Only write in the registers that are present in the CPU implementation.
  uint8_t count = arm64_hw_breakpoint_count();
  for (unsigned int i = 0; i < count; i++) {
    uint32_t* dbgbcr = &debug_state->hw_bps[i].dbgbcr;
    uint64_t* dbgbvr = &debug_state->hw_bps[i].dbgbvr;
    arm64_read_hw_breakpoint_by_index(i, dbgbcr, dbgbvr);
  }
}

// Writing Debug State ---------------------------------------------------------------------------

#define WRITE_HW_BREAKPOINT(index, dbgbcr_val, dbgbvr_val) \
  __arm_wsr("dbgbcr" #index "_el1", dbgbcr_val);           \
  __arm_wsr64("dbgbvr" #index "_el1", dbgbvr_val);         \
  __isb(ARM_MB_SY);
#define WRITE_HW_BREAKPOINT_CASE(index, dbgbcr_val, dbgbvr_val) \
  case index:                                                   \
    WRITE_HW_BREAKPOINT(index, dbgbcr_val, dbgbvr_val);         \
    break;

static void arm64_write_hw_breakpoint_by_index(unsigned int index, uint32_t dbgbcr,
                                               uint64_t dbgbvr) {
  switch (index) {
    WRITE_HW_BREAKPOINT_CASE(0, dbgbcr, dbgbvr);
    WRITE_HW_BREAKPOINT_CASE(1, dbgbcr, dbgbvr);
    WRITE_HW_BREAKPOINT_CASE(2, dbgbcr, dbgbvr);
    WRITE_HW_BREAKPOINT_CASE(3, dbgbcr, dbgbvr);
    WRITE_HW_BREAKPOINT_CASE(4, dbgbcr, dbgbvr);
    WRITE_HW_BREAKPOINT_CASE(5, dbgbcr, dbgbvr);
    WRITE_HW_BREAKPOINT_CASE(6, dbgbcr, dbgbvr);
    WRITE_HW_BREAKPOINT_CASE(7, dbgbcr, dbgbvr);
    WRITE_HW_BREAKPOINT_CASE(8, dbgbcr, dbgbvr);
    WRITE_HW_BREAKPOINT_CASE(9, dbgbcr, dbgbvr);
    WRITE_HW_BREAKPOINT_CASE(10, dbgbcr, dbgbvr);
    WRITE_HW_BREAKPOINT_CASE(11, dbgbcr, dbgbvr);
    WRITE_HW_BREAKPOINT_CASE(12, dbgbcr, dbgbvr);
    WRITE_HW_BREAKPOINT_CASE(13, dbgbcr, dbgbvr);
    WRITE_HW_BREAKPOINT_CASE(14, dbgbcr, dbgbvr);
    WRITE_HW_BREAKPOINT_CASE(15, dbgbcr, dbgbvr);
    default:
      DEBUG_ASSERT(false);
  }
}

#undef WRITE_HW_BREAKPOINT
#undef WRITE_HW_BREAKPOINT_CASE

#define WRITE_HW_WATCHPOINT(index, dbgwcr, dbgwvr) \
  __arm_wsr("dbgwcr" #index "_el1", dbgwcr);       \
  __arm_wsr64("dbgwvr" #index "_el1", dbgwvr);     \
  __isb(ARM_MB_SY);
#define WRITE_HW_WATCHPOINT_CASE(index, dbgwcr, dbgwvr) \
  case index:                                           \
    WRITE_HW_WATCHPOINT(index, dbgwcr, dbgwvr);         \
    break;

static void arm64_write_hw_watchpoint_by_index(unsigned int index, uint32_t dbgwcr,
                                               uint64_t dbgwvr) {
  switch (index) {
    WRITE_HW_WATCHPOINT_CASE(0, dbgwcr, dbgwvr);
    WRITE_HW_WATCHPOINT_CASE(1, dbgwcr, dbgwvr);
    WRITE_HW_WATCHPOINT_CASE(2, dbgwcr, dbgwvr);
    WRITE_HW_WATCHPOINT_CASE(3, dbgwcr, dbgwvr);
    WRITE_HW_WATCHPOINT_CASE(4, dbgwcr, dbgwvr);
    WRITE_HW_WATCHPOINT_CASE(5, dbgwcr, dbgwvr);
    WRITE_HW_WATCHPOINT_CASE(6, dbgwcr, dbgwvr);
    WRITE_HW_WATCHPOINT_CASE(7, dbgwcr, dbgwvr);
    WRITE_HW_WATCHPOINT_CASE(8, dbgwcr, dbgwvr);
    WRITE_HW_WATCHPOINT_CASE(9, dbgwcr, dbgwvr);
    WRITE_HW_WATCHPOINT_CASE(10, dbgwcr, dbgwvr);
    WRITE_HW_WATCHPOINT_CASE(11, dbgwcr, dbgwvr);
    WRITE_HW_WATCHPOINT_CASE(12, dbgwcr, dbgwvr);
    WRITE_HW_WATCHPOINT_CASE(13, dbgwcr, dbgwvr);
    WRITE_HW_WATCHPOINT_CASE(14, dbgwcr, dbgwvr);
    WRITE_HW_WATCHPOINT_CASE(15, dbgwcr, dbgwvr);
    default:
      DEBUG_ASSERT(false);
  }
}

#undef WRITE_HW_WATCHPOINT
#undef WRITE_HW_WATCHPOINT_CASE

void arm64_write_hw_debug_regs(const arm64_debug_state_t* debug_state) {
  // Write the HW Breakpoints.
  uint64_t bps_count = arm64_hw_breakpoint_count();
  for (unsigned int i = 0; i < bps_count; i++) {
    uint32_t dbgbcr = debug_state->hw_bps[i].dbgbcr;
    uint64_t dbgbvr = debug_state->hw_bps[i].dbgbvr;
    arm64_write_hw_breakpoint_by_index(i, dbgbcr, dbgbvr);
  }

  // Write the HW Watchpoints.
  uint64_t wps_count = arm64_hw_watchpoint_count();
  for (unsigned int i = 0; i < wps_count; i++) {
    uint32_t dbgwcr = debug_state->hw_wps[i].dbgwcr;
    uint64_t dbgwvr = debug_state->hw_wps[i].dbgwvr;
    arm64_write_hw_watchpoint_by_index(i, dbgwcr, dbgwvr);
  }
}

void arm64_clear_hw_debug_regs() {
  for (unsigned int i = 0; i < ARM64_MAX_HW_BREAKPOINTS; i++) {
    arm64_write_hw_breakpoint_by_index(i, 0, 0);
    arm64_write_hw_watchpoint_by_index(i, 0, 0);
  }
}

#ifndef NDEBUG

// Debug only.
void arm64_print_debug_registers(const arm64_debug_state_t* debug_state) {
  printf("HW breakpoints:\n");
  for (uint32_t i = 0; i < ARM64_MAX_HW_BREAKPOINTS; i++) {
    uint32_t dbgbcr = debug_state->hw_bps[i].dbgbcr;
    uint64_t dbgbvr = debug_state->hw_bps[i].dbgbvr;

    if (!ARM64_DBGBCR_E_GET(dbgbcr))
      continue;

    printf(
        "%02u. DBGBVR: 0x%lx, "
        "DBGBCR: E=%d, PMC=%d, BAS=%d, HMC=%d, SSC=%d, LBN=%d, BT=%d\n",
        i, dbgbvr, (int)(dbgbcr & ARM64_DBGBCR_E),
        (int)((dbgbcr & ARM64_DBGBCR_PMC_MASK) >> ARM64_DBGBCR_PMC_SHIFT),
        (int)((dbgbcr & ARM64_DBGBCR_BAS_MASK) >> ARM64_DBGBCR_BAS_SHIFT),
        (int)((dbgbcr & ARM64_DBGBCR_HMC_MASK) >> ARM64_DBGBCR_HMC_SHIFT),
        (int)((dbgbcr & ARM64_DBGBCR_SSC_MASK) >> ARM64_DBGBCR_SSC_SHIFT),
        (int)((dbgbcr & ARM64_DBGBCR_LBN_MASK) >> ARM64_DBGBCR_LBN_SHIFT),
        (int)((dbgbcr & ARM64_DBGBCR_BT_MASK) >> ARM64_DBGBCR_BT_SHIFT));
  }

  printf("HW watchpoints:\n");
  for (uint32_t i = 0; i < ARM64_MAX_HW_WATCHPOINTS; i++) {
    uint32_t dbgwcr = debug_state->hw_wps[i].dbgwcr;
    uint64_t dbgwvr = debug_state->hw_wps[i].dbgwvr;

    if (!ARM64_DBGWCR_E_GET(dbgwcr))
      continue;

    printf(
        "%02u. DBGWVR: 0x%lx, DBGWCR: "
        "E=%d, PAC=%d, LSC=%d, BAS=0x%x, HMC=%d, SSC=%d, LBN=%d, WT=%d, MASK=0x%x\n",
        i, dbgwvr, (int)(dbgwcr & ARM64_DBGWCR_E_MASK),
        (int)((dbgwcr & ARM64_DBGWCR_PAC_MASK) >> ARM64_DBGWCR_PAC_SHIFT),
        (int)((dbgwcr & ARM64_DBGWCR_LSC_MASK) >> ARM64_DBGWCR_LSC_SHIFT),
        (unsigned int)((dbgwcr & ARM64_DBGWCR_BAS_MASK) >> ARM64_DBGWCR_BAS_SHIFT),
        (int)((dbgwcr & ARM64_DBGWCR_HMC_MASK) >> ARM64_DBGWCR_HMC_SHIFT),
        (int)((dbgwcr & ARM64_DBGWCR_SSC_MASK) >> ARM64_DBGWCR_SSC_SHIFT),
        (int)((dbgwcr & ARM64_DBGWCR_LBN_MASK) >> ARM64_DBGWCR_LBN_SHIFT),
        (int)((dbgwcr & ARM64_DBGWCR_WT_MASK) >> ARM64_DBGWCR_WT_SHIFT),
        (unsigned int)((dbgwcr & ARM64_DBGWCR_MSK_MASK) >> ARM64_DBGWCR_MSK_SHIFT));
  }
}

void print_mdscr() {
  uint32_t mdscr = __arm_rsr("mdscr_el1");
  printf(
      "SS=%d, ERR=%d, TDCC=%d, KDE=%d, HDE=%d, MDE=%d, RAZ/WI=%d, "
      "TDA=%d, INTdis=%d, "
      "TXU=%d, RXO=%d, TXfull=%d, RXfull=%d\n",
      (int)((mdscr & ARM64_MDSCR_EL1_SS) >> ARM64_MDSCR_EL1_SS_SHIFT),
      (int)((mdscr & ARM64_MDSCR_EL1_ERR) >> ARM64_MDSCR_EL1_ERR_SHIFT),
      (int)((mdscr & ARM64_MDSCR_EL1_TDCC) >> ARM64_MDSCR_EL1_TDCC_SHIFT),
      (int)((mdscr & ARM64_MDSCR_EL1_KDE) >> ARM64_MDSCR_EL1_KDE_SHIFT),
      (int)((mdscr & ARM64_MDSCR_EL1_HDE) >> ARM64_MDSCR_EL1_HDE_SHIFT),
      (int)((mdscr & ARM64_MDSCR_EL1_MDE) >> ARM64_MDSCR_EL1_MDE_SHIFT),
      (int)((mdscr & ARM64_MDSCR_EL1_RAZ_WI) >> ARM64_MDSCR_EL1_RAZ_WI_SHIFT),
      (int)((mdscr & ARM64_MDSCR_EL1_TDA) >> ARM64_MDSCR_EL1_TDA_SHIFT),
      (int)((mdscr & ARM64_MDSCR_EL1_INTDIS) >> ARM64_MDSCR_EL1_INTDIS_SHIFT),
      (int)((mdscr & ARM64_MDSCR_EL1_TXU) >> ARM64_MDSCR_EL1_TXU_SHIFT),
      (int)((mdscr & ARM64_MDSCR_EL1_RXO) >> ARM64_MDSCR_EL1_RXO_SHIFT),
      (int)((mdscr & ARM64_MDSCR_EL1_TXfull) >> ARM64_MDSCR_EL1_TXfull_SHIFT),
      (int)((mdscr & ARM64_MDSCR_EL1_RXfull) >> ARM64_MDSCR_EL1_RXfull_SHIFT));
}

#endif
