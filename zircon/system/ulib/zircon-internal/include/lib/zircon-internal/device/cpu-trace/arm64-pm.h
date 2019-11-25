// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These definitions are used for communication between the cpu-trace
// device driver and the kernel only.

#pragma once

#include <lib/zircon-internal/device/cpu-trace/perf-mon.h>
#include <lib/zircon-internal/device/cpu-trace/common-pm.h>

#define ARM64_PMU_MASK(len, shift) (((1U << (len)) - 1) << (shift))

// Implementation values in pmcr.imp.
#define ARM64_PMCR_IMP_ARM 'A'

// Bits in the PMCR_EL0 register

#define ARM64_PMCR_EL0_E_SHIFT (0)
#define ARM64_PMCR_EL0_E_LEN (1)
#define ARM64_PMCR_EL0_E_MASK ARM64_PMU_MASK(ARM64_PMCR_EL0_E_LEN, ARM64_PMCR_EL0_E_SHIFT)

#define ARM64_PMCR_EL0_P_SHIFT (1)
#define ARM64_PMCR_EL0_P_LEN (1)
#define ARM64_PMCR_EL0_P_MASK ARM64_PMU_MASK(ARM64_PMCR_EL0_P_LEN, ARM64_PMCR_EL0_P_SHIFT)

#define ARM64_PMCR_EL0_C_SHIFT (2)
#define ARM64_PMCR_EL0_C_LEN (1)
#define ARM64_PMCR_EL0_C_MASK ARM64_PMU_MASK(ARM64_PMCR_EL0_C_LEN, ARM64_PMCR_EL0_C_SHIFT)

#define ARM64_PMCR_EL0_D_SHIFT (3)
#define ARM64_PMCR_EL0_D_LEN (1)
#define ARM64_PMCR_EL0_D_MASK ARM64_PMU_MASK(ARM64_PMCR_EL0_D_LEN, ARM64_PMCR_EL0_D_SHIFT)

#define ARM64_PMCR_EL0_X_SHIFT (4)
#define ARM64_PMCR_EL0_X_LEN (1)
#define ARM64_PMCR_EL0_X_MASK ARM64_PMU_MASK(ARM64_PMCR_EL0_X_LEN, ARM64_PMCR_EL0_X_SHIFT)

#define ARM64_PMCR_EL0_DP_SHIFT (5)
#define ARM64_PMCR_EL0_DP_LEN (1)
#define ARM64_PMCR_EL0_DP_MASK ARM64_PMU_MASK(ARM64_PMCR_EL0_DP_LEN, ARM64_PMCR_EL0_DP_SHIFT)

#define ARM64_PMCR_EL0_LC_SHIFT (6)
#define ARM64_PMCR_EL0_LC_LEN (1)
#define ARM64_PMCR_EL0_LC_MASK ARM64_PMU_MASK(ARM64_PMCR_EL0_LC_LEN, ARM64_PMCR_EL0_LC_SHIFT)

#define ARM64_PMCR_EL0_N_SHIFT (11)
#define ARM64_PMCR_EL0_N_LEN (5)
#define ARM64_PMCR_EL0_N_MASK ARM64_PMU_MASK(ARM64_PMCR_EL0_N_LEN, ARM64_PMCR_EL0_N_SHIFT)

#define ARM64_PMCR_EL0_IDCODE_SHIFT (16)
#define ARM64_PMCR_EL0_IDCODE_LEN (8)
#define ARM64_PMCR_EL0_IDCODE_MASK \
  ARM64_PMU_MASK(ARM64_PMCR_EL0_IDCODE_LEN, ARM64_PMCR_EL0_IDCODE_SHIFT)

#define ARM64_PMCR_EL0_IMP_SHIFT (24)
#define ARM64_PMCR_EL0_IMP_LEN (8)
#define ARM64_PMCR_EL0_IMP_MASK ARM64_PMU_MASK(ARM64_PMCR_EL0_IMP_LEN, ARM64_PMCR_EL0_IMP_SHIFT)

// Bits in the PMCCFILTR_EL0 register

#define ARM64_PMCCFILTR_EL0_M_SHIFT (26)
#define ARM64_PMCCFILTR_EL0_M_LEN (1)
#define ARM64_PMCCFILTR_EL0_M_MASK \
  ARM64_PMU_MASK(ARM64_PMCCFILTR_EL0_M_LEN, ARM64_PMCCFILTR_EL0_M_SHIFT)

#define ARM64_PMCCFILTR_EL0_NSH_SHIFT (27)
#define ARM64_PMCCFILTR_EL0_NSH_LEN (1)
#define ARM64_PMCCFILTR_EL0_NSH_MASK \
  ARM64_PMU_MASK(ARM64_PMCCFILTR_EL0_NSH_LEN, ARM64_PMCCFILTR_EL0_NSH_SHIFT)

#define ARM64_PMCCFILTR_EL0_NSU_SHIFT (28)
#define ARM64_PMCCFILTR_EL0_NSU_LEN (1)
#define ARM64_PMCCFILTR_EL0_NSU_MASK \
  ARM64_PMU_MASK(ARM64_PMCCFILTR_EL0_NSU_LEN, ARM64_PMCCFILTR_EL0_NSU_SHIFT)

#define ARM64_PMCCFILTR_EL0_NSK_SHIFT (29)
#define ARM64_PMCCFILTR_EL0_NSK_LEN (1)
#define ARM64_PMCCFILTR_EL0_NSK_MASK \
  ARM64_PMU_MASK(ARM64_PMCCFILTR_EL0_NSK_LEN, ARM64_PMCCFILTR_EL0_NSK_SHIFT)

#define ARM64_PMCCFILTR_EL0_U_SHIFT (30)
#define ARM64_PMCCFILTR_EL0_U_LEN (1)
#define ARM64_PMCCFILTR_EL0_U_MASK \
  ARM64_PMU_MASK(ARM64_PMCCFILTR_EL0_U_LEN, ARM64_PMCCFILTR_EL0_U_SHIFT)

#define ARM64_PMCCFILTR_EL0_P_SHIFT (31)
#define ARM64_PMCCFILTR_EL0_P_LEN (1)
#define ARM64_PMCCFILTR_EL0_P_MASK \
  ARM64_PMU_MASK(ARM64_PMCCFILTR_EL0_P_LEN, ARM64_PMCCFILTR_EL0_P_SHIFT)

// Bits in the PMCNTENCLR_EL0 register

#define ARM64_PMCNTENCLR_EL0_Pn_SHIFT (0)
#define ARM64_PMCNTENCLR_EL0_Pn_LEN (31)
#define ARM64_PMCNTENCLR_EL0_Pn_MASK \
  ARM64_PMU_MASK(ARM64_PMCNTENCLR_EL0_Pn_LEN, ARM64_PMCNTENCLR_EL0_Pn_SHIFT)

#define ARM64_PMCNTENCLR_EL0_C_SHIFT (31)
#define ARM64_PMCNTENCLR_EL0_C_LEN (1)
#define ARM64_PMCNTENCLR_EL0_C_MASK \
  ARM64_PMU_MASK(ARM64_PMCNTENCLR_EL0_C_LEN, ARM64_PMCNTENCLR_EL0_C_SHIFT)

// Bits in the PMCNTENSET_EL0 register

#define ARM64_PMCNTENSET_EL0_Pn_SHIFT (0)
#define ARM64_PMCNTENSET_EL0_Pn_LEN (31)
#define ARM64_PMCNTENSET_EL0_Pn_MASK \
  ARM64_PMU_MASK(ARM64_PMCNTENSET_EL0_Pn_LEN, ARM64_PMCNTENSET_EL0_Pn_SHIFT)

#define ARM64_PMCNTENSET_EL0_C_SHIFT (31)
#define ARM64_PMCNTENSET_EL0_C_LEN (1)
#define ARM64_PMCNTENSET_EL0_C_MASK \
  ARM64_PMU_MASK(ARM64_PMCNTENSET_EL0_C_LEN, ARM64_PMCNTENSET_EL0_C_SHIFT)

// Bits in the PMEVTYPERn_EL0 register

#define ARM64_PMEVTYPERn_EL0_EVCNT_SHIFT (0)
#define ARM64_PMEVTYPERn_EL0_EVCNT_LEN (16)
#define ARM64_PMEVTYPERn_EL0_EVCNT_MASK \
  ARM64_PMU_MASK(ARM64_PMEVTYPERn_EL0_EVCNT_LEN, ARM64_PMEVTYPERn_EL0_EVCNT_SHIFT)

#define ARM64_PMEVTYPERn_EL0_MT_SHIFT (25)
#define ARM64_PMEVTYPERn_EL0_MT_LEN (1)
#define ARM64_PMEVTYPERn_EL0_MT_MASK \
  ARM64_PMU_MASK(ARM64_PMEVTYPERn_EL0_MT_LEN, ARM64_PMEVTYPERn_EL0_MT_SHIFT)

#define ARM64_PMEVTYPERn_EL0_M_SHIFT (26)
#define ARM64_PMEVTYPERn_EL0_M_LEN (1)
#define ARM64_PMEVTYPERn_EL0_M_MASK \
  ARM64_PMU_MASK(ARM64_PMEVTYPERn_EL0_M_LEN, ARM64_PMEVTYPERn_EL0_M_SHIFT)

#define ARM64_PMEVTYPERn_EL0_NSH_SHIFT (27)
#define ARM64_PMEVTYPERn_EL0_NSH_LEN (1)
#define ARM64_PMEVTYPERn_EL0_NSH_MASK \
  ARM64_PMU_MASK(ARM64_PMEVTYPERn_EL0_NSH_LEN, ARM64_PMEVTYPERn_EL0_NSH_SHIFT)

#define ARM64_PMEVTYPERn_EL0_NSU_SHIFT (28)
#define ARM64_PMEVTYPERn_EL0_NSU_LEN (1)
#define ARM64_PMEVTYPERn_EL0_NSU_MASK \
  ARM64_PMU_MASK(ARM64_PMEVTYPERn_EL0_NSU_LEN, ARM64_PMEVTYPERn_EL0_NSU_SHIFT)

#define ARM64_PMEVTYPERn_EL0_NSK_SHIFT (29)
#define ARM64_PMEVTYPERn_EL0_NSK_LEN (1)
#define ARM64_PMEVTYPERn_EL0_NSK_MASK \
  ARM64_PMU_MASK(ARM64_PMEVTYPERn_EL0_NSK_LEN, ARM64_PMEVTYPERn_EL0_NSK_SHIFT)

#define ARM64_PMEVTYPERn_EL0_U_SHIFT (30)
#define ARM64_PMEVTYPERn_EL0_U_LEN (1)
#define ARM64_PMEVTYPERn_EL0_U_MASK \
  ARM64_PMU_MASK(ARM64_PMEVTYPERn_EL0_U_LEN, ARM64_PMEVTYPERn_EL0_U_SHIFT)

#define ARM64_PMEVTYPERn_EL0_P_SHIFT (31)
#define ARM64_PMEVTYPERn_EL0_P_LEN (1)
#define ARM64_PMEVTYPERn_EL0_P_MASK \
  ARM64_PMU_MASK(ARM64_PMEVTYPERn_EL0_P_LEN, ARM64_PMEVTYPERn_EL0_P_SHIFT)

// Bits in the PMINTENCLR_EL1 register

#define ARM64_PMINTENCLR_EL1_Pn_SHIFT (0)
#define ARM64_PMINTENCLR_EL1_Pn_LEN (31)
#define ARM64_PMINTENCLR_EL1_Pn_MASK \
  ARM64_PMU_MASK(ARM64_PMINTENCLR_EL1_Pn_LEN, ARM64_IMCNTENCLR_EL1_Pn_SHIFT)

#define ARM64_PMINTENCLR_EL1_C_SHIFT (31)
#define ARM64_PMINTENCLR_EL1_C_LEN (1)
#define ARM64_PMINTENCLR_EL1_C_MASK \
  ARM64_PMU_MASK(ARM64_PMINTENCLR_EL1_C_LEN, ARM64_IMCNTENCLR_EL1_C_SHIFT)

// Bits in the PMINTENSET_EL1 register

#define ARM64_PMINTENSET_EL1_Pn_SHIFT (0)
#define ARM64_PMINTENSET_EL1_Pn_LEN (31)
#define ARM64_PMINTENSET_EL1_Pn_MASK \
  ARM64_PMU_MASK(ARM64_PMINTENSET_EL1_Pn_LEN, ARM64_PMINTENSET_EL1_Pn_SHIFT)

#define ARM64_PMINTENSET_EL1_C_SHIFT (31)
#define ARM64_PMINTENSET_EL1_C_LEN (1)
#define ARM64_PMINTENSET_EL1_C_MASK \
  ARM64_PMU_MASK(ARM64_PMINTENSET_EL1_C_LEN, ARM64_PMINTENSET_EL1_C_SHIFT)

// Bits in the PMOVSCLR_EL0 register

#define ARM64_PMOVSCLR_EL0_Pn_SHIFT (0)
#define ARM64_PMOVSCLR_EL0_Pn_LEN (31)
#define ARM64_PMOVSCLR_EL0_Pn_MASK \
  ARM64_PMU_MASK(ARM64_PMOVSCLR_EL0_Pn_LEN, ARM64_PMOVSCLR_EL0_Pn_SHIFT)

#define ARM64_PMOVSCLR_EL0_C_SHIFT (31)
#define ARM64_PMOVSCLR_EL0_C_LEN (1)
#define ARM64_PMOVSCLR_EL0_C_MASK \
  ARM64_PMU_MASK(ARM64_PMOVSCLR_EL0_C_LEN, ARM64_PMOVSCLR_EL0_C_SHIFT)

// Bits in the PMOVSSET_EL0 register

#define ARM64_PMOVSSET_EL0_Pn_SHIFT (0)
#define ARM64_PMOVSSET_EL0_Pn_LEN (31)
#define ARM64_PMOVSSET_EL0_Pn_MASK \
  ARM64_PMU_MASK(ARM64_PMOVSSET_EL0_Pn_LEN, ARM64_PMOVSSET_EL0_Pn_SHIFT)

#define ARM64_PMOVSSET_EL0_C_SHIFT (31)
#define ARM64_PMOVSSET_EL0_C_LEN (1)
#define ARM64_PMOVSSET_EL0_C_MASK \
  ARM64_PMU_MASK(ARM64_PMOVSSET_EL0_C_LEN, ARM64_PMOVSSET_EL0_C_SHIFT)

// Bits in the PMSELR_EL0 register

#define ARM64_PMSELR_EL0_SEL_SHIFT (0)
#define ARM64_PMSELR_EL0_SEL_LEN (5)
#define ARM64_PMSELR_EL0_SLE_MASK \
  ARM64_PMU_MASK(ARM64_PMSELR_EL0_SEL_LEN, ARM64_PMSELR_EL0_SEL_SHIFT)

// Bits in the PMSWINC_EL0 register

#define ARM64_PMSWINC_EL0_Pn_SHIFT (0)
#define ARM64_PMSWINC_EL0_Pn_LEN (31)
#define ARM64_PMSWINC_EL0_Pn_MASK \
  ARM64_PMU_MASK(ARM64_PMSWINC_EL0_Pn_LEN, ARM64_PMSWINC_EL0_Pn_SHIFT)

// Bits in the PMUSERENR_EL0 register

#define ARM64_PMUSERENR_EL0_EN_SHIFT (0)
#define ARM64_PMUSERENR_EL0_EN_LEN (1)
#define ARM64_PMUSERENR_EL0_EN_MASK \
  ARM64_PMU_MASK(ARM64_PMUSERENR_EL0_EN_LEN, ARM64_PMUSERENR_EL0_EN_SHIFT)

#define ARM64_PMUSERENR_EL0_SW_SHIFT (1)
#define ARM64_PMUSERENR_EL0_SW_LEN (1)
#define ARM64_PMUSERENR_EL0_SW_MASK \
  ARM64_PMU_MASK(ARM64_PMUSERENR_EL0_SW_LEN, ARM64_PMUSERENR_EL0_SW_SHIFT)

#define ARM64_PMUSERENR_EL0_CR_SHIFT (2)
#define ARM64_PMUSERENR_EL0_CR_LEN (1)
#define ARM64_PMUSERENR_EL0_CR_MASK \
  ARM64_PMU_MASK(ARM64_PMUSERENR_EL0_CR_LEN, ARM64_PMUSERENR_EL0_CR_SHIFT)

#define ARM64_PMUSERENR_EL0_ER_SHIFT (3)
#define ARM64_PMUSERENR_EL0_ER_LEN (1)
#define ARM64_PMUSERENR_EL0_ER_MASK \
  ARM64_PMU_MASK(ARM64_PMUSERENR_EL0_ER_LEN, ARM64_PMUSERENR_EL0_ER_SHIFT)

// Compute mask for programmable counter COUNTER.
// This works for all the various counter sysregs.
#define ARM64_PMU_PROGRAMMABLE_COUNTER_MASK(counter) (1U << (counter))

// maximum number of programmable counters
// These are all "events" in our parlance, but on arm64 these are all
// counter events, so we use the more specific term "counter".
#define ARM64_PMU_MAX_PROGRAMMABLE_COUNTERS (6u)

// maximum number of fixed-use counters
// These are all "events" in our parlance, but on arm64 these are all
// counter events, so we use the more specific term "counter".
#define ARM64_PMU_MAX_FIXED_COUNTERS (1u)

///////////////////////////////////////////////////////////////////////////////

namespace perfmon {

// These structs are used for communication between the device driver
// and the kernel.

// Properties of perf data collection on this system.
struct Arm64PmuProperties {
  PmuCommonProperties common;
};

// Configuration data passed from driver to kernel.
struct Arm64PmuConfig {
  // The id of the timebase counter to use or |kEventIdNone|.
  // A "timebase counter" is used to trigger collection of data from other
  // events. In other words it sets the sample rate for those events.
  // If zero, then no timebase is in use: Each event must trigger its own
  // data collection. Otherwise the value is the id of the timebase counter
  // to use, which must appear in one of |programmable_ids| or |fixed_ids|.
  PmuEventId timebase_event;

  // Ids of each event. These values are written to the trace buffer to
  // identify the event.
  // The used entries begin at index zero and are consecutive (no holes).
  PmuEventId fixed_events[ARM64_PMU_MAX_FIXED_COUNTERS];
  PmuEventId programmable_events[ARM64_PMU_MAX_PROGRAMMABLE_COUNTERS];

  // Initial value of each counter.
  uint64_t fixed_initial_value[ARM64_PMU_MAX_FIXED_COUNTERS];
  uint32_t programmable_initial_value[ARM64_PMU_MAX_PROGRAMMABLE_COUNTERS];

  // Flags for each counter.
  // The values are |perfmon::kPmuConfigFlag*|.
  uint32_t fixed_flags[ARM64_PMU_MAX_FIXED_COUNTERS];
  uint32_t programmable_flags[ARM64_PMU_MAX_PROGRAMMABLE_COUNTERS];

  // H/W event numbers, one entry for each element in |programmable_events|.
  // There is only one fixed event, the cycle counter, so we don't need to
  // record its event number here.
  uint32_t programmable_hw_events[ARM64_PMU_MAX_PROGRAMMABLE_COUNTERS];

  uint8_t padding1[4];
};

}  // namespace perfmon

///////////////////////////////////////////////////////////////////////////////

// Flags for the events in arm64 *-pm-events.inc.

// Extra flags

// Architectural event
#define ARM64_PMU_REG_FLAG_ARCH 0x1
// Micro-architectural event, as defined by "ARM Architecture Reference Manual
// ARMv8", chapter D6 "The Performance Monitors Extension".
#define ARM64_PMU_REG_FLAG_MICROARCH 0x2
