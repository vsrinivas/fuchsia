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
    uint32_t mdscr_val = ARM64_READ_SYSREG_32(mdscr_el1) & ~ARM64_MDSCR_EL1_KDE;
    ARM64_WRITE_SYSREG(mdscr_el1, mdscr_val);
}

void arm64_enable_debug_state() {
    // The KDE bit enables and disables debug exceptions for the current execution.
    // Instruction Breakpoint Exceptions (software breakpoints) cannot be deactivated.
    uint32_t mdscr_val = ARM64_READ_SYSREG_32(mdscr_el1) | ARM64_MDSCR_EL1_KDE;
    ARM64_WRITE_SYSREG(mdscr_el1, mdscr_val);
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
    // TODO(donoso): Eventually this should be cached as a boot time constant.
    uint64_t dfr0 = ARM64_READ_SYSREG(id_aa64dfr0_el1);
    uint8_t count = (uint8_t)(((dfr0 & ARM64_ID_AADFR0_EL1_BRPS) >>
                               ARM64_ID_AADFR0_EL1_BRPS_SHIFT) +
                              1lu);
    // ARMv8 assures at least 2 hw registers.
    DEBUG_ASSERT(count >= 2 && count <= 16);
    return count;
}

// Read Debug State ------------------------------------------------------------------------------

static void arm64_read_hw_breakpoint_by_index(arm64_debug_state_t* debug_state,
                                              unsigned int index) {
    DEBUG_ASSERT(index < arm64_hw_breakpoint_count());

    switch (index) {
    case 0:
        debug_state->hw_bps[0].dbgbcr = ARM64_READ_SYSREG_32(dbgbcr0_el1);
        debug_state->hw_bps[0].dbgbvr = ARM64_READ_SYSREG(dbgbvr0_el1);
        break;
    case 1:
        debug_state->hw_bps[1].dbgbcr = ARM64_READ_SYSREG_32(dbgbcr1_el1);
        debug_state->hw_bps[1].dbgbvr = ARM64_READ_SYSREG(dbgbvr1_el1);
        break;
    case 2:
        debug_state->hw_bps[2].dbgbcr = ARM64_READ_SYSREG_32(dbgbcr2_el1);
        debug_state->hw_bps[2].dbgbvr = ARM64_READ_SYSREG(dbgbvr2_el1);
        break;
    case 3:
        debug_state->hw_bps[3].dbgbcr = ARM64_READ_SYSREG_32(dbgbcr3_el1);
        debug_state->hw_bps[3].dbgbvr = ARM64_READ_SYSREG(dbgbvr3_el1);
        break;
    case 4:
        debug_state->hw_bps[4].dbgbcr = ARM64_READ_SYSREG_32(dbgbcr4_el1);
        debug_state->hw_bps[4].dbgbvr = ARM64_READ_SYSREG(dbgbvr4_el1);
        break;
    case 5:
        debug_state->hw_bps[5].dbgbcr = ARM64_READ_SYSREG_32(dbgbcr5_el1);
        debug_state->hw_bps[5].dbgbvr = ARM64_READ_SYSREG(dbgbvr5_el1);
        break;
    case 6:
        debug_state->hw_bps[6].dbgbcr = ARM64_READ_SYSREG_32(dbgbcr6_el1);
        debug_state->hw_bps[6].dbgbvr = ARM64_READ_SYSREG(dbgbvr6_el1);
        break;
    case 7:
        debug_state->hw_bps[7].dbgbcr = ARM64_READ_SYSREG_32(dbgbcr7_el1);
        debug_state->hw_bps[7].dbgbvr = ARM64_READ_SYSREG(dbgbvr7_el1);
        break;
    case 8:
        debug_state->hw_bps[8].dbgbcr = ARM64_READ_SYSREG_32(dbgbcr8_el1);
        debug_state->hw_bps[8].dbgbvr = ARM64_READ_SYSREG(dbgbvr8_el1);
        break;
    case 9:
        debug_state->hw_bps[9].dbgbcr = ARM64_READ_SYSREG_32(dbgbcr9_el1);
        debug_state->hw_bps[9].dbgbvr = ARM64_READ_SYSREG(dbgbvr9_el1);
        break;
    case 10:
        debug_state->hw_bps[10].dbgbcr = ARM64_READ_SYSREG_32(dbgbcr10_el1);
        debug_state->hw_bps[10].dbgbvr = ARM64_READ_SYSREG(dbgbvr10_el1);
        break;
    case 11:
        debug_state->hw_bps[11].dbgbcr = ARM64_READ_SYSREG_32(dbgbcr11_el1);
        debug_state->hw_bps[11].dbgbvr = ARM64_READ_SYSREG(dbgbvr11_el1);
        break;
    case 12:
        debug_state->hw_bps[12].dbgbcr = ARM64_READ_SYSREG_32(dbgbcr12_el1);
        debug_state->hw_bps[12].dbgbvr = ARM64_READ_SYSREG(dbgbvr12_el1);
        break;
    case 13:
        debug_state->hw_bps[13].dbgbcr = ARM64_READ_SYSREG_32(dbgbcr13_el1);
        debug_state->hw_bps[13].dbgbvr = ARM64_READ_SYSREG(dbgbvr13_el1);
        break;
    case 14:
        debug_state->hw_bps[14].dbgbcr = ARM64_READ_SYSREG_32(dbgbcr14_el1);
        debug_state->hw_bps[14].dbgbvr = ARM64_READ_SYSREG(dbgbvr14_el1);
        break;
    case 15:
        debug_state->hw_bps[15].dbgbcr = ARM64_READ_SYSREG_32(dbgbcr15_el1);
        debug_state->hw_bps[15].dbgbvr = ARM64_READ_SYSREG(dbgbvr15_el1);
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
        ARM64_WRITE_SYSREG(dbgbcr0_el1, debug_state->hw_bps[0].dbgbcr);
        ARM64_WRITE_SYSREG(dbgbvr0_el1, debug_state->hw_bps[0].dbgbvr);
        break;
    case 1:
        ARM64_WRITE_SYSREG(dbgbcr1_el1, debug_state->hw_bps[1].dbgbcr);
        ARM64_WRITE_SYSREG(dbgbvr1_el1, debug_state->hw_bps[1].dbgbvr);
        break;
    case 2:
        ARM64_WRITE_SYSREG(dbgbcr2_el1, debug_state->hw_bps[2].dbgbcr);
        ARM64_WRITE_SYSREG(dbgbvr2_el1, debug_state->hw_bps[2].dbgbvr);
        break;
    case 3:
        ARM64_WRITE_SYSREG(dbgbcr3_el1, debug_state->hw_bps[3].dbgbcr);
        ARM64_WRITE_SYSREG(dbgbvr3_el1, debug_state->hw_bps[3].dbgbvr);
        break;
    case 4:
        ARM64_WRITE_SYSREG(dbgbcr4_el1, debug_state->hw_bps[4].dbgbcr);
        ARM64_WRITE_SYSREG(dbgbvr4_el1, debug_state->hw_bps[4].dbgbvr);
        break;
    case 5:
        ARM64_WRITE_SYSREG(dbgbcr5_el1, debug_state->hw_bps[5].dbgbcr);
        ARM64_WRITE_SYSREG(dbgbvr5_el1, debug_state->hw_bps[5].dbgbvr);
        break;
    case 6:
        ARM64_WRITE_SYSREG(dbgbcr6_el1, debug_state->hw_bps[6].dbgbcr);
        ARM64_WRITE_SYSREG(dbgbvr6_el1, debug_state->hw_bps[6].dbgbvr);
        break;
    case 7:
        ARM64_WRITE_SYSREG(dbgbcr7_el1, debug_state->hw_bps[7].dbgbcr);
        ARM64_WRITE_SYSREG(dbgbvr7_el1, debug_state->hw_bps[7].dbgbvr);
        break;
    case 8:
        ARM64_WRITE_SYSREG(dbgbcr8_el1, debug_state->hw_bps[8].dbgbcr);
        ARM64_WRITE_SYSREG(dbgbvr8_el1, debug_state->hw_bps[8].dbgbvr);
        break;
    case 9:
        ARM64_WRITE_SYSREG(dbgbcr9_el1, debug_state->hw_bps[9].dbgbcr);
        ARM64_WRITE_SYSREG(dbgbvr9_el1, debug_state->hw_bps[9].dbgbvr);
        break;
    case 10:
        ARM64_WRITE_SYSREG(dbgbcr10_el1, debug_state->hw_bps[10].dbgbcr);
        ARM64_WRITE_SYSREG(dbgbvr10_el1, debug_state->hw_bps[10].dbgbvr);
        break;
    case 11:
        ARM64_WRITE_SYSREG(dbgbcr11_el1, debug_state->hw_bps[11].dbgbcr);
        ARM64_WRITE_SYSREG(dbgbvr11_el1, debug_state->hw_bps[11].dbgbvr);
        break;
    case 12:
        ARM64_WRITE_SYSREG(dbgbcr12_el1, debug_state->hw_bps[12].dbgbcr);
        ARM64_WRITE_SYSREG(dbgbvr12_el1, debug_state->hw_bps[12].dbgbvr);
        break;
    case 13:
        ARM64_WRITE_SYSREG(dbgbcr13_el1, debug_state->hw_bps[13].dbgbcr);
        ARM64_WRITE_SYSREG(dbgbvr13_el1, debug_state->hw_bps[13].dbgbvr);
        break;
    case 14:
        ARM64_WRITE_SYSREG(dbgbcr14_el1, debug_state->hw_bps[14].dbgbcr);
        ARM64_WRITE_SYSREG(dbgbvr14_el1, debug_state->hw_bps[14].dbgbvr);
        break;
    case 15:
        ARM64_WRITE_SYSREG(dbgbcr15_el1, debug_state->hw_bps[15].dbgbcr);
        ARM64_WRITE_SYSREG(dbgbvr15_el1, debug_state->hw_bps[15].dbgbvr);
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
