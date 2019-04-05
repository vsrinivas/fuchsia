// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/asm_macros.h>

// The ARM SMCCC v1.0 calling convention provides the following guarantees about registers:
//
//  Register     Modified    Return State
//  X0...X3      Yes         Result values
//  X4...X17     Yes         Unpredictable
//  X18...X30    No          Preserved
//  SP_EL0       No          Preserved
//  SP_ELx       No          Preserved
//

// The arm_smccc_smc/hvc functions are almost direct calls to EL3/EL2. The AAPCS64 places the
// function parameters in x0-x7, which matches the SMCCC input parameters. The return value type
// is greater than 16 bytes, so the indirect result location register (x8) must be used to
// populate the result. Since this register is not preserved across the SMC call, it must be
// stored on the stack. For the purposes of AAPCS64 compatibility, the frame pointer (x29) is
// also stored on the stack. Returns x0-x3,x6.

// arm_smccc_result_t arm_smccc_smc(uint32_t w0, uint64_t x1, uint64_t x2, uint64_t x3,
//                                  uint64_t x4, uint64_t x5, uint64_t x6, uint32_t w7);
FUNCTION(arm_smccc_smc)
    push_regs x8, x29
    smc       #0
    pop_regs  x8, x29
    stp       x0, x1, [x8]
    stp       x2, x3, [x8, #16]
    str       x6, [x8, #32]
    ret
END_FUNCTION(arm_smccc_smc)

// arm_smccc_result_t arm_smccc_hvc(uint32_t w0, uint64_t x1, uint64_t x2, uint64_t x3,
//                                  uint64_t x4, uint64_t x5, uint64_t x6, uint32_t w7);
FUNCTION(arm_smccc_hvc)
    push_regs x8, x29
    hvc       #0
    pop_regs  x8, x29
    stp       x0, x1, [x8]
    stp       x2, x3, [x8, #16]
    ret
END_FUNCTION(arm_smccc_hvc)

