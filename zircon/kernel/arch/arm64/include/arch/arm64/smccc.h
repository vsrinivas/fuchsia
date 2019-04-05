// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/types.h>

__BEGIN_CDECLS

// ARM Secure Monitor Call Calling Convention (SMCCC)
//
// http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.den0028b/index.html

typedef struct arm_smccc_result {
    uint64_t x0;
    uint64_t x1;
    uint64_t x2;
    uint64_t x3;
    uint64_t x6; // at least one implementation uses it as a way to return session_id.
} arm_smccc_result_t;

arm_smccc_result_t arm_smccc_smc(uint32_t w0,              // Function Identifier
                                 uint64_t x1, uint64_t x2, // Parameters
                                 uint64_t x3, uint64_t x4, // Parameters
                                 uint64_t x5, uint64_t x6, // Parameters
                                 uint32_t w7);             // Client ID[15:0], Secure OS ID[31:16]

arm_smccc_result_t arm_smccc_hvc(uint32_t w0,              // Function Identifier
                                 uint64_t x1, uint64_t x2, // Parameters
                                 uint64_t x3, uint64_t x4, // Parameters
                                 uint64_t x5, uint64_t x6, // Parameters
                                 uint32_t w7);             // Secure OS ID[31:16]

__END_CDECLS
