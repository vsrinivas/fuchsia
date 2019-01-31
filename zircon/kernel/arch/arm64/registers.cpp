// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64.h>
#include <arch/arm64/registers.h>
#include <vm/vm.h>

void arm64_disable_debug_state() {
    // The KDE bit enables and disables debug exceptions for the current execution.
    // Instruction Breakpoint Exceptions (software breakpoints) cannot be deactivated.
    uint32_t mdscr_val = __arm_rsr("mdscr_el1") & ~ARM64_MDSCR_EL1_KDE;
    __arm_wsr("mdscr_el1", mdscr_val);
    __isb(ARM_MB_SY);
}

void arm64_enable_debug_state() {
    // The KDE bit enables and disables debug exceptions for the current execution.
    // Instruction Breakpoint Exceptions (software breakpoints) cannot be deactivated.
    uint32_t mdscr_val = __arm_rsr("mdscr_el1") | ARM64_MDSCR_EL1_KDE;
    __arm_wsr("mdscr_el1", mdscr_val);
    __isb(ARM_MB_SY);
}

bool arm64_validate_debug_state(arm64_debug_state_t* state) {
    // Validate that the addresses are valid.
    size_t hw_bp_count = arm64_hw_breakpoint_count();
    for (size_t i = 0; i < hw_bp_count; i++) {
        uint64_t addr = state->hw_bps[i].dbgbvr;
        if (addr != 0 && !is_user_address(addr)) {
            return false;
        }

        // Mask out the fields that userspace is not allowed to modify.
        uint32_t masked_user_bcr = state->hw_bps[i].dbgbcr & ARM64_DBGBCR_USER_MASK;
        state->hw_bps[i].dbgbcr = ARM64_DBGBCR_MASK | masked_user_bcr;
    }

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

static void arm64_read_hw_breakpoint_by_index(arm64_debug_state_t* debug_state,
                                              unsigned int index) {
    DEBUG_ASSERT(index < arm64_hw_breakpoint_count());

    switch (index) {
    case 0:
        debug_state->hw_bps[0].dbgbcr = __arm_rsr("dbgbcr0_el1");
        debug_state->hw_bps[0].dbgbvr = __arm_rsr64("dbgbvr0_el1");
        break;
    case 1:
        debug_state->hw_bps[1].dbgbcr = __arm_rsr("dbgbcr1_el1");
        debug_state->hw_bps[1].dbgbvr = __arm_rsr64("dbgbvr1_el1");
        break;
    case 2:
        debug_state->hw_bps[2].dbgbcr = __arm_rsr("dbgbcr2_el1");
        debug_state->hw_bps[2].dbgbvr = __arm_rsr64("dbgbvr2_el1");
        break;
    case 3:
        debug_state->hw_bps[3].dbgbcr = __arm_rsr("dbgbcr3_el1");
        debug_state->hw_bps[3].dbgbvr = __arm_rsr64("dbgbvr3_el1");
        break;
    case 4:
        debug_state->hw_bps[4].dbgbcr = __arm_rsr("dbgbcr4_el1");
        debug_state->hw_bps[4].dbgbvr = __arm_rsr64("dbgbvr4_el1");
        break;
    case 5:
        debug_state->hw_bps[5].dbgbcr = __arm_rsr("dbgbcr5_el1");
        debug_state->hw_bps[5].dbgbvr = __arm_rsr64("dbgbvr5_el1");
        break;
    case 6:
        debug_state->hw_bps[6].dbgbcr = __arm_rsr("dbgbcr6_el1");
        debug_state->hw_bps[6].dbgbvr = __arm_rsr64("dbgbvr6_el1");
        break;
    case 7:
        debug_state->hw_bps[7].dbgbcr = __arm_rsr("dbgbcr7_el1");
        debug_state->hw_bps[7].dbgbvr = __arm_rsr64("dbgbvr7_el1");
        break;
    case 8:
        debug_state->hw_bps[8].dbgbcr = __arm_rsr("dbgbcr8_el1");
        debug_state->hw_bps[8].dbgbvr = __arm_rsr64("dbgbvr8_el1");
        break;
    case 9:
        debug_state->hw_bps[9].dbgbcr = __arm_rsr("dbgbcr9_el1");
        debug_state->hw_bps[9].dbgbvr = __arm_rsr64("dbgbvr9_el1");
        break;
    case 10:
        debug_state->hw_bps[10].dbgbcr = __arm_rsr("dbgbcr10_el1");
        debug_state->hw_bps[10].dbgbvr = __arm_rsr64("dbgbvr10_el1");
        break;
    case 11:
        debug_state->hw_bps[11].dbgbcr = __arm_rsr("dbgbcr11_el1");
        debug_state->hw_bps[11].dbgbvr = __arm_rsr64("dbgbvr11_el1");
        break;
    case 12:
        debug_state->hw_bps[12].dbgbcr = __arm_rsr("dbgbcr12_el1");
        debug_state->hw_bps[12].dbgbvr = __arm_rsr64("dbgbvr12_el1");
        break;
    case 13:
        debug_state->hw_bps[13].dbgbcr = __arm_rsr("dbgbcr13_el1");
        debug_state->hw_bps[13].dbgbvr = __arm_rsr64("dbgbvr13_el1");
        break;
    case 14:
        debug_state->hw_bps[14].dbgbcr = __arm_rsr("dbgbcr14_el1");
        debug_state->hw_bps[14].dbgbvr = __arm_rsr64("dbgbvr14_el1");
        break;
    case 15:
        debug_state->hw_bps[15].dbgbcr = __arm_rsr("dbgbcr15_el1");
        debug_state->hw_bps[15].dbgbvr = __arm_rsr64("dbgbvr15_el1");
        break;
    default:
        DEBUG_ASSERT(false);
    }
}

void arm64_read_hw_debug_regs(arm64_debug_state_t* debug_state) {
    uint8_t count = arm64_hw_breakpoint_count();
    for (unsigned int i = 0; i < count; i++) {
        arm64_read_hw_breakpoint_by_index(debug_state, i);
    }
}

// Writing Debug State ---------------------------------------------------------------------------

static void arm64_write_hw_breakpoint_by_index(const arm64_debug_state_t* debug_state,
                                               unsigned int index) {
    DEBUG_ASSERT(index < arm64_hw_breakpoint_count());

    switch (index) {
    case 0:
        __arm_wsr("dbgbcr0_el1", debug_state->hw_bps[0].dbgbcr);
        __isb(ARM_MB_SY);
        __arm_wsr64("dbgbvr0_el1", debug_state->hw_bps[0].dbgbvr);
        __isb(ARM_MB_SY);
        break;
    case 1:
        __arm_wsr("dbgbcr1_el1", debug_state->hw_bps[1].dbgbcr);
        __isb(ARM_MB_SY);
        __arm_wsr64("dbgbvr1_el1", debug_state->hw_bps[1].dbgbvr);
        __isb(ARM_MB_SY);
        break;
    case 2:
        __arm_wsr("dbgbcr2_el1", debug_state->hw_bps[2].dbgbcr);
        __isb(ARM_MB_SY);
        __arm_wsr64("dbgbvr2_el1", debug_state->hw_bps[2].dbgbvr);
        __isb(ARM_MB_SY);
        break;
    case 3:
        __arm_wsr("dbgbcr3_el1", debug_state->hw_bps[3].dbgbcr);
        __isb(ARM_MB_SY);
        __arm_wsr64("dbgbvr3_el1", debug_state->hw_bps[3].dbgbvr);
        __isb(ARM_MB_SY);
        break;
    case 4:
        __arm_wsr("dbgbcr4_el1", debug_state->hw_bps[4].dbgbcr);
        __isb(ARM_MB_SY);
        __arm_wsr64("dbgbvr4_el1", debug_state->hw_bps[4].dbgbvr);
        __isb(ARM_MB_SY);
        break;
    case 5:
        __arm_wsr("dbgbcr5_el1", debug_state->hw_bps[5].dbgbcr);
        __isb(ARM_MB_SY);
        __arm_wsr64("dbgbvr5_el1", debug_state->hw_bps[5].dbgbvr);
        __isb(ARM_MB_SY);
        break;
    case 6:
        __arm_wsr("dbgbcr6_el1", debug_state->hw_bps[6].dbgbcr);
        __isb(ARM_MB_SY);
        __arm_wsr64("dbgbvr6_el1", debug_state->hw_bps[6].dbgbvr);
        __isb(ARM_MB_SY);
        break;
    case 7:
        __arm_wsr("dbgbcr7_el1", debug_state->hw_bps[7].dbgbcr);
        __isb(ARM_MB_SY);
        __arm_wsr64("dbgbvr7_el1", debug_state->hw_bps[7].dbgbvr);
        __isb(ARM_MB_SY);
        break;
    case 8:
        __arm_wsr("dbgbcr8_el1", debug_state->hw_bps[8].dbgbcr);
        __isb(ARM_MB_SY);
        __arm_wsr64("dbgbvr8_el1", debug_state->hw_bps[8].dbgbvr);
        __isb(ARM_MB_SY);
        break;
    case 9:
        __arm_wsr("dbgbcr9_el1", debug_state->hw_bps[9].dbgbcr);
        __isb(ARM_MB_SY);
        __arm_wsr64("dbgbvr9_el1", debug_state->hw_bps[9].dbgbvr);
        __isb(ARM_MB_SY);
        break;
    case 10:
        __arm_wsr("dbgbcr10_el1", debug_state->hw_bps[10].dbgbcr);
        __isb(ARM_MB_SY);
        __arm_wsr64("dbgbvr10_el1", debug_state->hw_bps[10].dbgbvr);
        __isb(ARM_MB_SY);
        break;
    case 11:
        __arm_wsr("dbgbcr11_el1", debug_state->hw_bps[11].dbgbcr);
        __isb(ARM_MB_SY);
        __arm_wsr64("dbgbvr11_el1", debug_state->hw_bps[11].dbgbvr);
        __isb(ARM_MB_SY);
        break;
    case 12:
        __arm_wsr("dbgbcr12_el1", debug_state->hw_bps[12].dbgbcr);
        __isb(ARM_MB_SY);
        __arm_wsr64("dbgbvr12_el1", debug_state->hw_bps[12].dbgbvr);
        __isb(ARM_MB_SY);
        break;
    case 13:
        __arm_wsr("dbgbcr13_el1", debug_state->hw_bps[13].dbgbcr);
        __isb(ARM_MB_SY);
        __arm_wsr64("dbgbvr13_el1", debug_state->hw_bps[13].dbgbvr);
        __isb(ARM_MB_SY);
        break;
    case 14:
        __arm_wsr("dbgbcr14_el1", debug_state->hw_bps[14].dbgbcr);
        __isb(ARM_MB_SY);
        __arm_wsr64("dbgbvr14_el1", debug_state->hw_bps[14].dbgbvr);
        __isb(ARM_MB_SY);
        break;
    case 15:
        __arm_wsr("dbgbcr15_el1", debug_state->hw_bps[15].dbgbcr);
        __isb(ARM_MB_SY);
        __arm_wsr64("dbgbvr15_el1", debug_state->hw_bps[15].dbgbvr);
        __isb(ARM_MB_SY);
        break;
    default:
        DEBUG_ASSERT(false);
    }
}

void arm64_write_hw_debug_regs(const arm64_debug_state_t* debug_state) {
    uint64_t bps_count = arm64_hw_breakpoint_count();
    for (unsigned int i = 0; i < bps_count; i++) {
        arm64_write_hw_breakpoint_by_index(debug_state, i);
    }
}
