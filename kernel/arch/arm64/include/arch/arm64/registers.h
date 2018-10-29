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
#define ARM64_MDSCR_EL1_ERR (1u << 6)
#define ARM64_MDSCR_EL1_TDCC (1u << 12)
#define ARM64_MDSCR_EL1_KDE (1u << 13)
#define ARM64_MDSCR_EL1_HDE (1u << 14)
#define ARM64_MDSCR_EL1_MDE (1u << 15)
#define ARM64_MDSCR_EL1_RAZ_WI 0x000e0000lu
#define ARM64_MDSCR_EL1_RAZ_WI_SHIFT 16
#define ARM64_MDSCR_EL1_TDA (1u << 21)
#define ARM64_MDSCR_EL1_INTDIS = 0x000c0000
#define ARM64_MDSCR_EL1_INTDIS_SHIFT 22
#define ARM64_MDSCR_EL1_TXU (1u << 26)
#define ARM64_MDSCR_EL1_RXO (1u << 27)
#define ARM64_MDSCR_EL1_TXfull (1u << 29)
#define ARM64_MDSCR_EL1_RXfull (1u << 30)

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
#define ARM64_DBGBCR_PMC        (0x3u << 1)  // Bits 1-2.
#define ARM64_DBGBCR_PMC_SHIFT  1u
#define ARM64_DBGBCR_BAS        (0xFu << 5)  // Bits 5-8.
#define ARM64_DBGBCR_HMC        (1u << 13)
#define ARM64_DBGBCR_SSC        (0x3u << 14) // Bits 14-15.
#define ARM64_DBGBCR_LBN        (0xFu << 16) // Bits 16-19.
#define ARM64_DBGBCR_BT         (0xFu << 20) // Bits 20-23.

// The user can only activate/deactivate breakpoints.
#define ARM64_DBGBCR_USER_MASK (ARM64_DBGBCR_E)

// This is the mask that we validate for a breakpoint control.
// PMC [0b10]
// BAS [0b1111]: Match on complete address.
// HMC [0]
// SSC [0]
// LBN [0]: No breakpoint linking.
// BT [0]: Unliked instruction address match.
//
// The PMC, HMC, SSC values configured here enable debug exceptions to be thrown in EL0.
#define ARM64_DBGBCR_MASK ((0b10u << ARM64_DBGBCR_PMC_SHIFT) | \
                           ARM64_DBGBCR_BAS)

#define ARM64_MAX_HW_BREAKPOINTS 16

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
  // This field is the BRPs field on ID_AADFR0_EL1 (plus one), which doesn't change.
  // Ideally we could just query this information from a static place calculated on boot time
  // instead of having to have this value around for every thread.
  uint8_t hw_bps_count;

  // TODO(donosoc): Do watchpoint integration.
} arm64_debug_state_t;


/* Disables the HW debug functionalities for the current thread.
 * There is no "enable" call. To do this, use arm_write_debug_state. */
void arm64_disable_debug_state(void);
/* Checks whether the given state is valid to install on a running thread.
 * Will mask out reserved values on DBGBCR<n>. This is for the caller convenience, considering
 * that we don't have a good mechanism to communicate back to the user what went wrong with the
 * call. */
bool arm64_validate_debug_state(arm64_debug_state_t *debug_state);

/* Returns the amount of HW breakpoints present in this CPU. */
uint8_t arm64_hw_breakpoint_count(void);

/* Read from the CPU registers into |debug_state|. */
void arm64_read_hw_debug_regs(arm64_debug_state_t* debug_state);

/* Write from the |debug_state| into the CPU registers.
 *
 * IMPORTANT: This function is used in the context switch, so no validation is done, just writing.
 *            In any other context (eg. setting debug values from a syscall), you *MUST* call
 *            arm64_validate_debug_state first. */
void arm64_write_hw_debug_regs(const arm64_debug_state_t* debug_state);

__END_CDECLS
