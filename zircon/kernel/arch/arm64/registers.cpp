// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64.h>
#include <arch/arm64/registers.h>
#include <kernel/thread.h>
#include <vm/vm.h>

void arm64_set_debug_state_for_thread(thread* thread, bool active) {
    arm64_iframe_t* iframe = thread->arch.suspended_general_regs;
    DEBUG_ASSERT(iframe);
    if (active) {
        iframe->mdscr |= ARM64_MDSCR_EL1_MDE;   // MDE enables hardware exceptions.
        iframe->mdscr |= ARM64_MDSCR_EL1_KDE;   // KDE enables local debugging in EL0.
    } else {
        iframe->mdscr &= ~ARM64_MDSCR_EL1_MDE;   // MDE enables hardware exceptions.
        iframe->mdscr &= ~ARM64_MDSCR_EL1_KDE;   // KDE enables local debugging in EL0.
    }
}

void arm64_set_debug_state_for_cpu(bool active) {
    uint32_t mdscr = __arm_rsr("mdscr_el1");
    if (active) {
        mdscr |= ARM64_MDSCR_EL1_MDE;   // MDE enables hardware exceptions.
        mdscr |= ARM64_MDSCR_EL1_KDE;   // KDE enables local debugging in EL0.
    } else {
        mdscr &= ~ARM64_MDSCR_EL1_MDE;   // MDE enables hardware exceptions.
        mdscr &= ~ARM64_MDSCR_EL1_KDE;   // KDE enables local debugging in EL0.
    }
    __arm_wsr("mdscr_el1", mdscr);
    __isb(ARM_MB_SY);
}

bool arm64_validate_debug_state(arm64_debug_state_t* state, uint32_t* active_breakpoints) {
    uint32_t breakpoint_count = 0;
    // Validate that the addresses are valid.
    size_t hw_bp_count = arm64_hw_breakpoint_count();
    for (size_t i = 0; i < ARM64_MAX_HW_BREAKPOINTS; i++) {
        uint32_t dbgbcr = state->hw_bps[i].dbgbcr;
        uint64_t dbgbvr = state->hw_bps[i].dbgbvr;

        // If we're beyond the provided values and a breakpoint is set, we
        // consider these parameters invalid.
        if ((i >= hw_bp_count) && (dbgbcr || dbgbvr)) {
            return false;
        }

        // Verify that the breakpoint refers to userspace.
        if (dbgbvr != 0 && !is_user_address(dbgbvr)) {
            return false;
        }
        state->hw_bps[i].dbgbvr &= ARM64_DBGBVR_USER_MASK;

        // If the address is valid and the breakpoint is activated, we mask in
        // the other necessary value and count it for bookkeepping.
        if (state->hw_bps[i].dbgbcr & ARM64_DBGBCR_E) {
            state->hw_bps[i].dbgbcr = ARM64_DBGBCR_ACTIVATED_MASK;
            breakpoint_count++;
        }
    }

    *active_breakpoints = breakpoint_count;
    return true;
}

uint8_t arm64_hw_breakpoint_count() {
    uint64_t dfr0 = __arm_rsr64("id_aa64dfr0_el1");
    uint8_t count = (uint8_t)(((dfr0 & ARM64_ID_AADFR0_EL1_BRPS) >>
                               ARM64_ID_AADFR0_EL1_BRPS_SHIFT) + 1lu);
    // ARMv8 assures at least 2 hw registers.
    DEBUG_ASSERT(count >= ARM64_MIN_HW_BREAKPOINTS &&
                 count <= ARM64_MAX_HW_BREAKPOINTS);
    return count;
}

uint8_t arm64_hw_watchpoint_count() {
    uint64_t dfr0 = __arm_rsr64("id_aa64dfr0_el1");
    uint8_t count = (uint8_t)(((dfr0 & ARM64_ID_AADFR0_EL1_WRPS) >>
                               ARM64_ID_AADFR0_EL1_WRPS_SHIFT) + 1lu);
    // ARMv8 assures at least 2 hw registers.
    DEBUG_ASSERT(count >= ARM64_MIN_HW_WATCHPOINTS &&
                 count <= ARM64_MAX_HW_WATCHPOINTS);
    return count;
}

// Read Debug State ------------------------------------------------------------------------------

#define READ_HW_BREAKPOINT(index, dbgbcr_val, dbgbvr_val) \
    dbgbcr_val = __arm_rsr("dbgbcr" #index "_el1");       \
    dbgbvr_val = __arm_rsr64("dbgbvr" #index "_el1");
#define READ_HW_BREAKPOINT_CASE(index, dbgbcr_val, dbgbvr_val) \
    case index:                                                \
        READ_HW_BREAKPOINT(index, dbgbcr_val, dbgbvr_val);     \
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
    __arm_wsr("dbgbcr" #index "_el1", dbgbcr_val);         \
    __arm_wsr64("dbgbvr" #index "_el1", dbgbvr_val);       \
    __isb(ARM_MB_SY);
#define WRITE_HW_BREAKPOINT_CASE(index, dbgbcr_val, dbgbvr_val) \
    case index:                                                 \
        WRITE_HW_BREAKPOINT(index, dbgbcr_val, dbgbvr_val);     \
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

void arm64_write_hw_debug_regs(const arm64_debug_state_t* debug_state) {
    uint64_t bps_count = arm64_hw_breakpoint_count();
    for (unsigned int i = 0; i < bps_count; i++) {
        uint32_t dbgbcr = debug_state->hw_bps[i].dbgbcr;
        uint64_t dbgbvr = debug_state->hw_bps[i].dbgbvr;
        arm64_write_hw_breakpoint_by_index(i, dbgbcr, dbgbvr);
    }
}

void arm64_clear_hw_debug_regs() {
  for (unsigned int i = 0; i < ARM64_MAX_HW_BREAKPOINTS; i++) {
        arm64_write_hw_breakpoint_by_index(0, 0, i);
  }
}

// Debug only.
void arm64_print_debug_registers(const arm64_debug_state_t* debug_state) {
    for (size_t i = 0; i < ARM64_MAX_HW_BREAKPOINTS; i++) {
        uint32_t dbgbcr = debug_state->hw_bps[i].dbgbcr;
        uint64_t dbgbvr = debug_state->hw_bps[i].dbgbvr;

        printf("%lu. DBGBVR: 0x%lx, DBGBCR: E=%d, PMC=%d, BAS=%d, HMC=%d, SSC=%d, LBN=%d, BT=%d\n",
               i, dbgbvr,
               (int)(dbgbcr & ARM64_DBGBCR_E),
               (int)((dbgbcr & ARM64_DBGBCR_PMC) >> ARM64_DBGBCR_PMC_SHIFT),
               (int)((dbgbcr & ARM64_DBGBCR_BAS) >> ARM64_DBGGCR_BAS_SHIFT),
               (int)((dbgbcr & ARM64_DBGBCR_HMC) >> ARM64_DBGBCR_HMC_SHIFT),
               (int)((dbgbcr & ARM64_DBGBCR_SSC) >> ARM64_DBGBCR_SSC_SHIFT),
               (int)((dbgbcr & ARM64_DBGBCR_LBN) >> ARM64_DBGBCR_LBN_SHIFT),
               (int)((dbgbcr & ARM64_DBGBCR_BT) >> ARM64_DBGBCR_BY_SHIFT));
    }
}

void print_mdscr() {
    uint32_t mdscr = __arm_rsr("mdscr_el1");
    printf("SS=%d, ERR=%d, TDCC=%d, KDE=%d, HDE=%d, MDE=%d, RAZ/WI=%d, "
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


