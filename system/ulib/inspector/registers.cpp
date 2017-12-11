// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include <zircon/syscalls.h>

#include "utils-impl.h"
#include "inspector/registers.h"

namespace inspector {

bool read_general_regs(zx_handle_t thread, general_regs_type* regs) {
    // The syscall takes a uint32_t.
    auto to_xfer = static_cast<uint32_t>(sizeof(*regs));
    uint32_t bytes_read;
    auto status = zx_thread_read_state(thread, ZX_THREAD_STATE_REGSET0, regs, to_xfer, &bytes_read);
    if (status < 0) {
        print_zx_error("unable to access general regs", status);
        return false;
    }
    if (bytes_read != to_xfer) {
        print_error("general regs size mismatch: %u != %u\n", bytes_read, to_xfer);
        return false;
    }
    return true;
}

#if defined(__x86_64__)

void print_general_regs(const general_regs_type* regs,
                        const excp_data_type* excp_data) {
    printf(" CS:  %#18llx RIP: %#18" PRIx64 " EFL: %#18" PRIx64,
           0ull, regs->rip, regs->rflags);
    if (excp_data) {
        printf(" CR2: %#18" PRIx64, excp_data->cr2);
    }
    printf("\n");
    printf(" RAX: %#18" PRIx64 " RBX: %#18" PRIx64 " RCX: %#18" PRIx64 " RDX: %#18" PRIx64 "\n",
           regs->rax, regs->rbx, regs->rcx, regs->rdx);
    printf(" RSI: %#18" PRIx64 " RDI: %#18" PRIx64 " RBP: %#18" PRIx64 " RSP: %#18" PRIx64 "\n",
           regs->rsi, regs->rdi, regs->rbp, regs->rsp);
    printf("  R8: %#18" PRIx64 "  R9: %#18" PRIx64 " R10: %#18" PRIx64 " R11: %#18" PRIx64 "\n",
           regs->r8, regs->r9, regs->r10, regs->r11);
    printf(" R12: %#18" PRIx64 " R13: %#18" PRIx64 " R14: %#18" PRIx64 " R15: %#18" PRIx64 "\n",
           regs->r12, regs->r13, regs->r14, regs->r15);
    if (excp_data) {
        // errc value is 17 on purpose, errc is 4 characters
        printf(" errc: %#17" PRIx64 "\n", excp_data->err_code);
    }
}

#elif defined(__aarch64__)

void print_general_regs(const general_regs_type* regs,
                        const excp_data_type* excp_data) {
    printf(" x0  %#18" PRIx64 " x1  %#18" PRIx64 " x2  %#18" PRIx64 " x3  %#18" PRIx64 "\n",
           regs->r[0], regs->r[1], regs->r[2], regs->r[3]);
    printf(" x4  %#18" PRIx64 " x5  %#18" PRIx64 " x6  %#18" PRIx64 " x7  %#18" PRIx64 "\n",
           regs->r[4], regs->r[5], regs->r[6], regs->r[7]);
    printf(" x8  %#18" PRIx64 " x9  %#18" PRIx64 " x10 %#18" PRIx64 " x11 %#18" PRIx64 "\n",
           regs->r[8], regs->r[9], regs->r[10], regs->r[11]);
    printf(" x12 %#18" PRIx64 " x13 %#18" PRIx64 " x14 %#18" PRIx64 " x15 %#18" PRIx64 "\n",
           regs->r[12], regs->r[13], regs->r[14], regs->r[15]);
    printf(" x16 %#18" PRIx64 " x17 %#18" PRIx64 " x18 %#18" PRIx64 " x19 %#18" PRIx64 "\n",
           regs->r[16], regs->r[17], regs->r[18], regs->r[19]);
    printf(" x20 %#18" PRIx64 " x21 %#18" PRIx64 " x22 %#18" PRIx64 " x23 %#18" PRIx64 "\n",
           regs->r[20], regs->r[21], regs->r[22], regs->r[23]);
    printf(" x24 %#18" PRIx64 " x25 %#18" PRIx64 " x26 %#18" PRIx64 " x27 %#18" PRIx64 "\n",
           regs->r[24], regs->r[25], regs->r[26], regs->r[27]);
    printf(" x28 %#18" PRIx64 " x29 %#18" PRIx64 " lr  %#18" PRIx64 " sp  %#18" PRIx64 "\n",
           regs->r[28], regs->r[29], regs->lr, regs->sp);
    printf(" pc  %#18" PRIx64 " psr %#18" PRIx64 "\n",
           regs->pc, regs->cpsr);
}

#else   // unsupported arch

void print_general_regs(const general_regs_type* regs,
                        const excp_data_type* excp_data) {
    printf("unsupported architecture\n");
}

#endif

}  // namespace inspector
