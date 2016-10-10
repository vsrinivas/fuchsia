// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hexdump/hexdump.h>

#include <magenta/assert.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls-debug.h>

#include "backtrace.h"
#include "utils.h"

const char* excp_type_to_str(uint32_t type) {
    DEBUG_ASSERT(MX_EXCP_IS_ARCH(type));
    switch (type) {
    case MX_EXCP_GENERAL:
        return "general fault";
    case MX_EXCP_FATAL_PAGE_FAULT:
        return "fatal page fault";
    case MX_EXCP_UNDEFINED_INSTRUCTION:
        return "undefined instruction";
    case MX_EXCP_SW_BREAKPOINT:
        return "sw breakpoint";
    case MX_EXCP_HW_BREAKPOINT:
        return "hw breakpoint";
    default:
        // Note: To get a compilation failure when a new exception type has
        // been added without having also updated this function, compile with
        // -Wswitch-enum.
        return "unknown fault";
    }
}

constexpr uint64_t kSysExceptionKey = 1166444u;

void output_frame_x86_64(const x86_64_exc_data_t& exc_data,
                         const mx_x86_64_general_regs_t& regs) {
    printf(" CS:  %#18llx RIP: %#18" PRIx64 " EFL: %#18" PRIx64 " CR2: %#18" PRIx64 "\n",
           0ull, regs.rip, regs.rflags, exc_data.cr2);
    printf(" RAX: %#18" PRIx64 " RBX: %#18" PRIx64 " RCX: %#18" PRIx64 " RDX: %#18" PRIx64 "\n",
           regs.rax, regs.rbx, regs.rcx, regs.rdx);
    printf(" RSI: %#18" PRIx64 " RDI: %#18" PRIx64 " RBP: %#18" PRIx64 " RSP: %#18" PRIx64 "\n",
           regs.rsi, regs.rdi, regs.rbp, regs.rsp);
    printf("  R8: %#18" PRIx64 "  R9: %#18" PRIx64 " R10: %#18" PRIx64 " R11: %#18" PRIx64 "\n",
           regs.r8, regs.r9, regs.r10, regs.r11);
    printf(" R12: %#18" PRIx64 " R13: %#18" PRIx64 " R14: %#18" PRIx64 " R15: %#18" PRIx64 "\n",
           regs.r12, regs.r13, regs.r14, regs.r15);
    // errc value is 17 on purpose, errc is 4 characters
    printf(" errc: %#17" PRIx64 "\n", exc_data.err_code);
}

void output_frame_arm64(const arm64_exc_data_t& exc_data,
                        const mx_aarch64_general_regs_t& regs) {
    printf(" x0  %#18" PRIx64 " x1  %#18" PRIx64 " x2  %#18" PRIx64 " x3  %#18" PRIx64 "\n",
           regs.r[0], regs.r[1], regs.r[2], regs.r[3]);
    printf(" x4  %#18" PRIx64 " x5  %#18" PRIx64 " x6  %#18" PRIx64 " x7  %#18" PRIx64 "\n",
           regs.r[4], regs.r[5], regs.r[6], regs.r[7]);
    printf(" x8  %#18" PRIx64 " x9  %#18" PRIx64 " x10 %#18" PRIx64 " x11 %#18" PRIx64 "\n",
           regs.r[8], regs.r[9], regs.r[10], regs.r[11]);
    printf(" x12 %#18" PRIx64 " x13 %#18" PRIx64 " x14 %#18" PRIx64 " x15 %#18" PRIx64 "\n",
           regs.r[12], regs.r[13], regs.r[14], regs.r[15]);
    printf(" x16 %#18" PRIx64 " x17 %#18" PRIx64 " x18 %#18" PRIx64 " x19 %#18" PRIx64 "\n",
           regs.r[16], regs.r[17], regs.r[18], regs.r[19]);
    printf(" x20 %#18" PRIx64 " x21 %#18" PRIx64 " x22 %#18" PRIx64 " x23 %#18" PRIx64 "\n",
           regs.r[20], regs.r[21], regs.r[22], regs.r[23]);
    printf(" x24 %#18" PRIx64 " x25 %#18" PRIx64 " x26 %#18" PRIx64 " x27 %#18" PRIx64 "\n",
           regs.r[24], regs.r[25], regs.r[26], regs.r[27]);
    printf(" x28 %#18" PRIx64 " x29 %#18" PRIx64 " lr  %#18" PRIx64 " sp  %#18" PRIx64 "\n",
           regs.r[28], regs.r[29], regs.lr, regs.sp);
    printf(" pc  %#18" PRIx64 " psr %#18" PRIx64 "\n",
           regs.pc, regs.cpsr);
};

void dump_memory(mx_handle_t proc, uintptr_t start, uint32_t len) {
    auto buf = static_cast<uint8_t*>(malloc(len));
    auto res = mx_debug_read_memory(proc, start, len, buf);
    if (res < 0) {
        printf("failed reading %p memory; error : %" PRIdPTR "\n", (void*)start, res);
    } else if (res != 0) {
        hexdump(buf, (uint32_t)res);
    }
    free(buf);
}

void process_report(const mx_exception_report_t* report) {
    if (!MX_EXCP_IS_ARCH(report->header.type))
        return;

    auto context = report->context;
    printf("<== fatal exception: process [%" PRIu64 "] thread [%" PRIu64 "]\n", context.pid, context.tid);
    printf("<== %s , PC at 0x%" PRIxPTR "\n", excp_type_to_str(report->header.type), context.arch.pc);

    auto process = mx_debug_task_get_child(0, context.pid);
    if (process <= 0) {
        printf("failed to get a handle to [%" PRIu64 "] : error %d\n", context.pid, process);
        return;
    }
    auto thread = mx_debug_task_get_child(process, context.tid);
    if (thread <= 0) {
        printf("failed to get a handle to [%" PRIu64 ".%" PRIu64 "] : error %d\n", context.pid, context.tid, thread);
        mx_handle_close(process);
        return;
    }

#if defined(__x86_64__)
    mx_x86_64_general_regs_t regs;
#elif defined(__aarch64__)
    mx_aarch64_general_regs_t regs;
#else // unsupported arch
    int regs;
#endif

    uint32_t regs_size = sizeof(regs);
    auto status = mx_thread_read_state(thread, MX_THREAD_STATE_REGSET0, &regs, &regs_size);
    if (status < 0) {
        printf("unable to read general regs for [%" PRIu64 ".%" PRIu64 "] : error %d\n", context.pid, context.tid, status);
        goto Fail;
    }
    if (regs_size != sizeof(regs)) {
        printf("general regs size mismatch for [%" PRIu64 ".%" PRIu64 "] : %u != %zu\n", context.pid, context.tid, regs_size, sizeof(regs));
        goto Fail;
    }

    if (context.arch_id == ARCH_ID_X86_64) {
#if defined(__x86_64__)
        output_frame_x86_64(context.arch.u.x86_64, regs);
        printf("bottom of user stack:\n");
        dump_memory(process, regs.rsp, 256u);
        printf("arch: x86_64\n");
        backtrace(process, regs.rip, regs.rbp);
#endif
    } else if (context.arch_id == ARCH_ID_ARM_64) {
#if defined(__aarch64__)
        output_frame_arm64(context.arch.u.arm_64, regs);

        // Only output the Fault address register if there's a data fault.
        if (MX_EXCP_FATAL_PAGE_FAULT == report->header.type)
            printf(" far %#18" PRIx64 "\n", context.arch.u.arm_64.far);

        printf("bottom of user stack:\n");
        dump_memory(process, regs.sp, 256u);
        printf("arch: aarch64\n");
        backtrace(process, regs.pc, regs.sp);
#endif
    } else {
        // TODO: support other architectures. It's unlikely we'll get here as
        // trying to read the regs will likely fail, but we don't assume that.
        printf("unsupported architecture .. coming soon.\n");
    }

Fail:
    debugf(1, "Done handling thread %" PRIu64 ".%" PRIu64 ".\n", get_koid(process), get_koid(thread));

    // allow the thread (and then process) to die:
    mx_task_resume(thread, MX_RESUME_EXCEPTION | MX_RESUME_NOT_HANDLED);

    mx_handle_close(thread);
    mx_handle_close(process);
}

// A small wrapper to provide a useful name to the API call used to effect
// the request.

mx_status_t bind_system_exception_port(mx_handle_t eport) {
    return mx_object_bind_exception_port(MX_HANDLE_INVALID, eport, kSysExceptionKey, 0);
}

// A small wrapper to provide a useful name to the API call used to effect
// the request.

mx_status_t unbind_system_exception_port() {
    return mx_object_bind_exception_port(MX_HANDLE_INVALID, MX_HANDLE_INVALID,
                                         kSysExceptionKey, 0);
}

void usage() {
    fprintf(stderr, "Usage: crashlogger [options]\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -d[n] = set debug level to N\n");
    fprintf(stderr, "  -f = force replacement of existing crashlogger\n");
}

int main(int argc, char** argv) {
    mx_status_t status;
    bool force = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (strncmp(arg, "-d", 2) == 0) {
            if (arg[2] != '\0') {
                debug_level = atoi(arg + 2);
            } else {
                debug_level = 1;
            }
        } else if (strcmp(arg, "-f") == 0) {
            force = true;
        } else {
            usage();
            return 1;
        }
    }

    // If asked, undo any previously installed exception port.
    // This is useful if the system gets in a state where we want to replace
    // an existing crashlogger with this one.
    if (force) {
        status = unbind_system_exception_port();
        if (status != NO_ERROR) {
            print_mx_error("unable to unbind system exception port", status);
            return 1;
        }
    }

    mx_handle_t ex_port = mx_port_create(0u);
    if (ex_port < 0) {
        print_mx_error("mx_port_create failed", ex_port);
        return 1;
    }

    status = bind_system_exception_port(ex_port);
    if (status < 0) {
        print_mx_error("unable to bind system exception port", status);
        return 1;
    }

    printf("crashlogger service ready\n");

    while (true) {
        mx_exception_packet_t packet;
        mx_port_wait(ex_port, &packet, sizeof(packet));
        if (packet.hdr.key != kSysExceptionKey) {
            print_error("invalid crash key");
            return 1;
        }

        process_report(&packet.report);
    }

    return 0;
}
