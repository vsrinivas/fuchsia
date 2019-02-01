// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

namespace debug_ipc {

// Overall functionality for interpreting arm64 specific information.
// This is defined in debug_ipc because both the client and the debug agent
// need to access this information.

// Macros for obtaining the mask of an arm64 flag.
// Usage:
//    FLAG_MASK(CpsrEL)
#define _ARM64_FLAG_MASK(shift, mask) ((uint64_t)((mask) << (shift)))
#define ARM64_FLAG_MASK(reg, flag)                         \
  _ARM64_FLAG_MASK(::debug_ipc::k##reg##_##flag##_##Shift, \
                   ::debug_ipc::k##reg##_##flag##_##Mask)

// Macros for obtaining the value of an arm64 flag.
// Usage:
//    FLAG_VALUE(value, CpsrV)
#define _ARM64_FLAG_VALUE(value, shift, mask) \
  ((uint8_t)((value >> shift) & mask))
#define ARM64_FLAG_VALUE(value, reg, flag)                         \
  _ARM64_FLAG_VALUE(value, ::debug_ipc::k##reg##_##flag##_##Shift, \
                    ::debug_ipc::k##reg##_##flag##_##Mask)

// CPSR ------------------------------------------------------------------------

constexpr uint64_t kCpsr_EL_Shift = 0;  // Exception Level
constexpr uint64_t kCpsr_EL_Mask = 0x1;
constexpr uint64_t kCpsr_F_Shift = 6;  // FIQ mask bit.
constexpr uint64_t kCpsr_F_Mask = 0x1;
constexpr uint64_t kCpsr_I_Shift = 7;  // IRQ mask bit.
constexpr uint64_t kCpsr_I_Mask = 0x1;
constexpr uint64_t kCpsr_A_Shift = 8;  // SError mask bit.
constexpr uint64_t kCpsr_A_Mask = 0x1;
constexpr uint64_t kCpsr_D_Shift = 9;  // Debug exception mask bit.
constexpr uint64_t kCpsr_D_Mask = 0x1;
constexpr uint64_t kCpsr_IL_Shift = 20;  // Illegal Execution bit.
constexpr uint64_t kCpsr_IL_Mask = 0x1;
constexpr uint64_t kCpsr_SS_Shift = 21;  // Single Step.
constexpr uint64_t kCpsr_SS_Mask = 0x1;
constexpr uint64_t kCpsr_PAN_Shift = 22;  // Privilege Access Never.
constexpr uint64_t kCpsr_PAN_Mask = 0x1;
constexpr uint64_t kCpsr_UAO_Shift = 23;  // Load/Store privilege access.
constexpr uint64_t kCpsr_UAO_Mask = 0x1;

constexpr uint64_t kCpsr_V_Shift = 28;  // Overflow bit.
constexpr uint64_t kCpsr_V_Mask = 0x1;
constexpr uint64_t kCpsr_C_Shift = 29;  // Carry bit.
constexpr uint64_t kCpsr_C_Mask = 0x1;
constexpr uint64_t kCpsr_Z_Shift = 30;  // Zero bit.
constexpr uint64_t kCpsr_Z_Mask = 0x1;
constexpr uint64_t kCpsr_N_Shift = 31;  // Negative bit.
constexpr uint64_t kCpsr_N_Mask = 0x1;

// DBGBCR ----------------------------------------------------------------------

constexpr uint64_t kDBGBCR_E_Shift = 0;
constexpr uint64_t kDBGBCR_E_Mask = 0b1;
constexpr uint64_t kDBGBCR_PMC_Shift = 1;
constexpr uint64_t kDBGBCR_PMC_Mask = 0b11;
constexpr uint64_t kDBGBCR_BAS_Shift = 5;
constexpr uint64_t kDBGBCR_BAS_Mask = 0b1111;
constexpr uint64_t kDBGBCR_HMC_Shift = 13;
constexpr uint64_t kDBGBCR_HMC_Mask = 0b1;
constexpr uint64_t kDBGBCR_SSC_Shift = 14;
constexpr uint64_t kDBGBCR_SSC_Mask = 0b11;
constexpr uint64_t kDBGBCR_LBN_Shift = 16;
constexpr uint64_t kDBGBCR_LBN_Mask = 0b1111;
constexpr uint64_t kDBGBCR_BT_Shift = 20;
constexpr uint64_t kDBGBCR_BT_Mask = 0b1111;

// ID_AA64DFR0_EL1 -------------------------------------------------------------

constexpr uint64_t kID_AA64DFR0_EL1_DV_Shift = 0;  // Debug Version.
constexpr uint64_t kID_AA64DFR0_EL1_DV_Mask = 0b1111;
constexpr uint64_t kID_AA64DFR0_EL1_TV_Shift = 4;  // Trace Version.
constexpr uint64_t kID_AA64DFR0_EL1_TV_Mask = 0b1111;
constexpr uint64_t kID_AA64DFR0_EL1_PMUV_Shift = 8;  // PMU Version.
constexpr uint64_t kID_AA64DFR0_EL1_PMUV_Mask = 0b1111;
// HW breakpoint count (value is count - 1).
constexpr uint64_t kID_AA64DFR0_EL1_BRP_Shift = 12;
constexpr uint64_t kID_AA64DFR0_EL1_BRP_Mask = 0b1111;
// HW watchpoint count (value is count - 1).
constexpr uint64_t kID_AA64DFR0_EL1_WRP_Shift = 20;
constexpr uint64_t kID_AA64DFR0_EL1_WRP_Mask = 0b1111;
// Number of breakpoints that are context-aware (value is count - 1).
// These are the highest numbered breakpoints.
// TODO(donosoc): Actually find out what this means.
constexpr uint64_t kID_AA64DFR0_EL1_CTX_CMP_Shift = 28;
constexpr uint64_t kID_AA64DFR0_EL1_CTX_CMP_Mask = 0b1111;
// Statistical Profiling Extension version.
constexpr uint64_t kID_AA64DFR0_EL1_PMSV_Shift = 32;
constexpr uint64_t kID_AA64DFR0_EL1_PMSV_Mask = 0b1111;

// MDSCR_EL1 -------------------------------------------------------------------

constexpr uint64_t kMDSCR_EL1_SS_Shift = 0;
constexpr uint64_t kMDSCR_EL1_SS_Mask = 0b1;
constexpr uint64_t kMDSCR_EL1_ERR_Shift = 6;
constexpr uint64_t kMDSCR_EL1_ERR_Mask = 0b1;
constexpr uint64_t kMDSCR_EL1_TDCC_Shift = 12;
constexpr uint64_t kMDSCR_EL1_TDCC_Mask = 0b1;
constexpr uint64_t kMDSCR_EL1_KDE_Shift = 13;
constexpr uint64_t kMDSCR_EL1_KDE_Mask = 0b1;
constexpr uint64_t kMDSCR_EL1_HDE_Shift = 14;
constexpr uint64_t kMDSCR_EL1_HDE_Mask = 0b1;
constexpr uint64_t kMDSCR_EL1_MDE_Shift = 15;
constexpr uint64_t kMDSCR_EL1_MDE_Mask = 0b1;
constexpr uint64_t kMDSCR_EL1_RAZ_WI_Shift = 16;
constexpr uint64_t kMDSCR_EL1_RAZ_WI_Mask = 0b111;
constexpr uint64_t kMDSCR_EL1_TDA_Shift = 21;
constexpr uint64_t kMDSCR_EL1_TDA_Mask = 0b1;
constexpr uint64_t kMDSCR_EL1_INTdis_Shift = 22;
constexpr uint64_t kMDSCR_EL1_INTdis_Mask = 0b11;
constexpr uint64_t kMDSCR_EL1_TXU_Shift = 26;
constexpr uint64_t kMDSCR_EL1_TXU_Mask = 0b1;
constexpr uint64_t kMDSCR_EL1_RXO_Shift = 27;
constexpr uint64_t kMDSCR_EL1_RXO_Mask = 0b1;
constexpr uint64_t kMDSCR_EL1_TXfull_Shift = 29;
constexpr uint64_t kMDSCR_EL1_TXfull_Mask = 0b1;
constexpr uint64_t kMDSCR_EL1_RXfull_Shift = 30;
constexpr uint64_t kMDSCR_EL1_RXfull_Mask = 0b1;

}  // namespace debug_ipc
