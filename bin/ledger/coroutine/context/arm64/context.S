// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/tls.h>

#include "peridot/bin/ledger/coroutine/context/arm64/context.h"


#define FUNCTION(x) .global x; .type x,%function; x:

// GetContext
// Captures the state of the CPU in the passed struct.
// The saved registers are:
// - All callee saved registers.
// - SP, Unsafe SP, LR and ARG0.
FUNCTION(_ZN7context10GetContextEPNS_15InternalContextE)
    stp x19, x20, [x0, #X19_O]
    stp x21, x22, [x0, #X21_O]
    stp x23, x24, [x0, #X23_O]
    stp x25, x26, [x0, #X25_O]
    stp x27, x28, [x0, #X27_O]
    stp x29, x30, [x0, #X29_O]
    mov x2,  sp
    str x2,  [x0, #SP_O]
    stp d8,  d9,  [x0, #D8_O]
    stp d10, d11, [x0, #D10_O]
    stp d12, d13, [x0, #D12_O]
    stp d14, d15, [x0, #D14_O]
    mov x1,  #0
    str x1,  [x0, #X0_O]

    mrs x1, TPIDR_EL0
    ldr x1, [x1, #ZX_TLS_UNSAFE_SP_OFFSET]
    str x1, [x0, #UNSAFE_SP_O]

    mov x0,  #0xFFFFFFFFFFFFFFFF
    ret

// SetContext
// Restores the state of the CPU from the passed struct.
// The restored registers are:
// - All callee saved registers.
// - SP, Unsafe SP, LR and ARG0.
FUNCTION(_ZN7context10SetContextEPNS_15InternalContextE)
    // The sanitizer runtime wants to be informed of non-local exits.
    // Call __asan_handle_no_return() before doing the actual longjmp.
#if __has_feature(address_sanitizer)
    // Save the argument register in a callee-saves register
    // around calling __asan_handle_no_return.  We don't need
    // to save the return address since we'll never return to it.
    mov x19, x0
    bl __asan_handle_no_return
    mov x0, x19
#endif

    ldp x19, x20, [x0, #X19_O]
    ldp x21, x22, [x0, #X21_O]
    ldp x23, x24, [x0, #X23_O]
    ldp x25, x26, [x0, #X25_O]
    ldp x27, x28, [x0, #X27_O]
    ldp x29, x30, [x0, #X29_O]
    ldr x2,  [x0, #SP_O]
    mov sp,  x2
    ldp d8 , d9,  [x0, #D8_O]
    ldp d10, d11, [x0, #D10_O]
    ldp d12, d13, [x0, #D12_O]
    ldp d14, d15, [x0, #D14_O]

    ldr x2,  [x0, #UNSAFE_SP_O]
    mrs x1, TPIDR_EL0
    str x2, [x1, #ZX_TLS_UNSAFE_SP_OFFSET]

    ldr x0, [x0, #X0_O]
    br x30
