// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

// MDSCR_EL1
// Monitor Debug System Control Register. It's the main control register fot the debug
// implementation.

#define ARM64_MDSCR_EL1_SS (1u << 0)
#define ARM64_MDSCR_EL1_SS_SHIFT 0
#define ARM64_MDSCR_EL1_ERR (1u << 6)
#define ARM64_MDSCR_EL1_ERR_SHIFT 6
#define ARM64_MDSCR_EL1_TDCC (1u << 12)
#define ARM64_MDSCR_EL1_TDCC_SHIFT 12
#define ARM64_MDSCR_EL1_KDE (1u << 13)
#define ARM64_MDSCR_EL1_KDE_SHIFT 13
#define ARM64_MDSCR_EL1_HDE (1u << 14)
#define ARM64_MDSCR_EL1_HDE_SHIFT 14
#define ARM64_MDSCR_EL1_MDE (1u << 15)
#define ARM64_MDSCR_EL1_MDE_SHIFT 15
#define ARM64_MDSCR_EL1_RAZ_WI 0x000e0000lu
#define ARM64_MDSCR_EL1_RAZ_WI_SHIFT 16
#define ARM64_MDSCR_EL1_TDA (1u << 21)
#define ARM64_MDSCR_EL1_TDA_SHIFT 21
#define ARM64_MDSCR_EL1_INTDIS 0x000c0000u
#define ARM64_MDSCR_EL1_INTDIS_SHIFT 22
#define ARM64_MDSCR_EL1_TXU (1u << 26)
#define ARM64_MDSCR_EL1_TXU_SHIFT 26
#define ARM64_MDSCR_EL1_RXO (1u << 27)
#define ARM64_MDSCR_EL1_RXO_SHIFT 27
#define ARM64_MDSCR_EL1_TXfull (1u << 29)
#define ARM64_MDSCR_EL1_TXfull_SHIFT 29
#define ARM64_MDSCR_EL1_RXfull (1u << 30)
#define ARM64_MDSCR_EL1_RXfull_SHIFT 30

// ID_AA64DFR0
// Debug Feature Register 0. This register is used to query the system for the debug
// capabilites present within the chip.

#define ARM64_ID_AADFR0_EL1_DEBUG_VER   0x0000000000000Flu
#define ARM64_ID_AADFR0_EL1_TRACE_VER   0x000000000000F0lu
#define ARM64_ID_AADFR0_EL1_PMU_VER `   0x00000000000F00lu
// Defines the amount of HW breakpoints.
#define ARM64_ID_AADFR0_EL1_BRPS        0x0000000000F000lu
#define ARM64_ID_AADFR0_EL1_BRPS_SHIFT  12lu
// Defines the amount of HW data watchpoints.
#define ARM64_ID_AADFR0_EL1_WRPS        0x00000000F00000lu
#define ARM64_ID_AADFR0_EL1_WRPS_SHIFT  20lu
#define ARM64_ID_AADFR0_EL1_CTX_CMP     0x000000F0000000lu
#define ARM64_ID_AADFR0_EL1_PMS_VER     0x00000F00000000lu

// DBGBCR<n>
// Control register for HW breakpoints. There is one foreach HW breakpoint present within the
// system. They go numbering by DBGBCR0, DBGBCR1, ... until the value defined in ID_AADFR0_EL1.

#define ARM64_DBGBCR_E          (1u << 0)
#define ARM64_DBGBCR_PMC        (0b11u << 1)  // Bits 1-2.
#define ARM64_DBGBCR_PMC_SHIFT  1u
#define ARM64_DBGBCR_BAS        (0b1111u << 5)  // Bits 5-8.
#define ARM64_DBGGCR_BAS_SHIFT  5u
#define ARM64_DBGBCR_HMC        (1u << 13)
#define ARM64_DBGBCR_HMC_SHIFT  13u
#define ARM64_DBGBCR_SSC        (0b111u << 14) // Bits 14-15.
#define ARM64_DBGBCR_SSC_SHIFT  14u
#define ARM64_DBGBCR_LBN        (0b1111u << 16) // Bits 16-19.
#define ARM64_DBGBCR_LBN_SHIFT  16u
#define ARM64_DBGBCR_BT         (0b1111u << 20) // Bits 20-23.
#define ARM64_DBGBCR_BY_SHIFT   20u

// The user can only activate/deactivate breakpoints.
#define ARM64_DBGBCR_USER_MASK (ARM64_DBGBCR_E)
// This mask is applied when a breakpoint is activated. We control the configuration of the
// breakpoint so the user only needs to set the E bit.
#define ARM64_DBGBCR_ACTIVATED_MASK (ARM64_DBGBCR_E |                    \
                                     (0b10u << ARM64_DBGBCR_PMC_SHIFT) | \
                                     ARM64_DBGBCR_BAS)
// The actual addresses bits that we will allow the user to write. The rest of the values
// will be masked.
#define ARM64_DBGBVR_USER_MASK (0xfffffffffffcu)

// This is the mask that we validate for a breakpoint control.
// PMC [0b10]
// BAS [0b1111]: Match on complete address.
// HMC [0]
// SSC [0]
// LBN [0]: No breakpoint linking.
// BT [0]: Unliked instruction address match.
//
// ARMv8 assures at least 2 hw registers.
#define ARM64_MIN_HW_BREAKPOINTS 2
#define ARM64_MAX_HW_BREAKPOINTS 16
#define ARM64_MIN_HW_WATCHPOINTS 2
#define ARM64_MAX_HW_WATCHPOINTS 16

#include <zircon/compiler.h>
#include <sys/types.h>

__BEGIN_CDECLS

/* Kernel tracking of the current state of the debug registers for a particular thread.
 * ARMv8 can have from 2 to 16 HW breakpoints and 2 to 16 HW watchpoints.
 *
 * This struct can potentially hold all of them. If the platform has fewer of those
 * breakpoints available, it will fill from the lower index up to correct amount.
 * The other indices should never be accessed. */
typedef struct arm64_debug_state {
  struct {
    uint32_t dbgbcr;
    uint64_t dbgbvr;
  } hw_bps[ARM64_MAX_HW_BREAKPOINTS];
  uint32_t esr;
  // TODO(donosoc): Do watchpoint integration.
} arm64_debug_state_t;

/* Enable/disable the HW debug functionalities for the current thread. */
void arm64_set_debug_state_for_thread(thread*, bool active);
/* Enable/disable mdscr_el1 */
void arm64_set_debug_state_for_cpu(bool active);

/* Checks whether the given state is valid to install on a running thread.
 *
 * Any breakpoint information set beyong the valid breakpoint count given by the platform will
 * return false.
 *
 * Will validate that all the given values within a DBGBVR register are either 0 or a valid
 * userspace address.
 *
 * Will mask out reserved values on DBGBCR<n>. This is for the caller convenience, considering
 * that we don't have a good mechanism to communicate back to the user what went wrong with the
 * call.
 *
 * If returning true, |active_breakpoints| will be the number of activated breakpoints within
 * set given |debug_state|.
 * */
bool arm64_validate_debug_state(arm64_debug_state_t* debug_state,
                                uint32_t* active_breakpoints);

/* Returns the amount of HW breakpoints present in this CPU. */
uint8_t arm64_hw_breakpoint_count();
uint8_t arm64_hw_watchpoint_count();

/* Read from the CPU registers into |debug_state|. */
void arm64_read_hw_debug_regs(arm64_debug_state_t* debug_state);

/* Write from the |debug_state| into the CPU registers.
 *
 * IMPORTANT: This function is used in the context switch, so no validation is done, just writing.
 *            In any other context (eg. setting debug values from a syscall), you *MUST* call
 *            arm64_validate_debug_state first. */
void arm64_write_hw_debug_regs(const arm64_debug_state_t* debug_state);
// Will zero out the debug registers for the current CPOU.
void arm64_clear_hw_debug_regs();

/* Handles the context switch for debug HW functionality.
 * Will only copy over state if it's enabled (non-zero) for |new_thread|. */
void arm64_debug_state_context_switch(thread* old_thread, thread* new_thread);

// Debug only.
void arm64_print_debug_registers(const arm64_debug_state_t*);
void arm64_print_mdscr();

__END_CDECLS
