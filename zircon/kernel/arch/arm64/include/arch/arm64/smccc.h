// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_SMCCC_H_
#define ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_SMCCC_H_

#include <zircon/types.h>

#include <kernel/auto_preempt_disabler.h>

__BEGIN_CDECLS

// ARM Secure Monitor Call Calling Convention (SMCCC)
//
// http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.den0028b/index.html

typedef struct arm_smccc_result {
  uint64_t x0;
  uint64_t x1;
  uint64_t x2;
  uint64_t x3;
  uint64_t x6;  // at least one implementation uses it as a way to return session_id.
} arm_smccc_result_t;

// Calls the low-level SMC function with preemption disabled.
inline arm_smccc_result_t arm_smccc_smc(uint32_t w0,               // Function Identifier
                                        uint64_t x1, uint64_t x2,  // Parameters
                                        uint64_t x3, uint64_t x4,  // Parameters
                                        uint64_t x5, uint64_t x6,  // Parameters
                                        uint32_t w7) {  // Client ID[15:0], Secure OS ID[31:16]
  extern arm_smccc_result_t arm_smccc_smc_internal(uint32_t w0, uint64_t x1, uint64_t x2,
                                                   uint64_t x3, uint64_t x4, uint64_t x5,
                                                   uint64_t x6, uint32_t w7);
  AutoPreemptDisabler<APDInitialState::PREEMPT_DISABLED> disabler;
  return arm_smccc_smc_internal(w0, x1, x2, x3, x4, x5, x6, w7);
}

// Calls the low-level HVC function with preemption disabled.
inline arm_smccc_result_t arm_smccc_hvc(uint32_t w0,               // Function Identifier
                                        uint64_t x1, uint64_t x2,  // Parameters
                                        uint64_t x3, uint64_t x4,  // Parameters
                                        uint64_t x5, uint64_t x6,  // Parameters
                                        uint32_t w7) {             // Secure OS ID[31:16]
  extern arm_smccc_result_t arm_smccc_hvc_internal(uint32_t w0, uint64_t x1, uint64_t x2,
                                                   uint64_t x3, uint64_t x4, uint64_t x5,
                                                   uint64_t x6, uint32_t w7);
  AutoPreemptDisabler<APDInitialState::PREEMPT_DISABLED> disabler;
  return arm_smccc_hvc_internal(w0, x1, x2, x3, x4, x5, x6, w7);
}

__END_CDECLS

#endif  // ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_SMCCC_H_
