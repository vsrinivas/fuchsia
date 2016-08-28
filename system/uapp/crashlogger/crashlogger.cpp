// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <hexdump/hexdump.h>

#include <magenta/syscalls.h>

#include "backtrace.h"

enum : uint32_t {
    EXC_FATAL_PAGE_FAULT,
    EXC_UNDEFINED_INSTRUCTION,
    EXC_GENERAL,
};

const char* exc_type_to_str(uint32_t type) {
    switch (type) {
    case EXC_FATAL_PAGE_FAULT:
        return "fatal page fault";
    case EXC_UNDEFINED_INSTRUCTION:
        return "undefined instruction";
    case EXC_GENERAL:
        return "general fault";
    default:
        return "unknown fault";
    }
}

constexpr uint64_t kSysExceptionKey = 1166444u;

void print_error(int line, const char* what = "") {
    fprintf(stderr, "crashlogger: ln%d : %s\n", line, what);
}

void output_frame_x86_64(const x86_64_exc_frame_t& frame) {
    printf(" CS:  %#18llx RIP: %#18llx EFL: %#18llx CR2: %#18lx\n",
           frame.cs, frame.ip, frame.flags, 0ul);
    printf(" RAX: %#18llx RBX: %#18llx RCX: %#18llx RDX: %#18llx\n",
           frame.rax, frame.rbx, frame.rcx, frame.rdx);
    printf(" RSI: %#18llx RDI: %#18llx RBP: %#18llx RSP: %#18llx\n",
           frame.rsi, frame.rdi, frame.rbp, frame.user_sp);
    printf("  R8: %#18llx  R9: %#18llx R10: %#18llx R11: %#18llx\n",
           frame.r8, frame.r9, frame.r10, frame.r11);
    printf(" R12: %#18llx R13: %#18llx R14: %#18llx R15: %#18llx\n",
           frame.r12, frame.r13, frame.r14, frame.r15);
    printf(" errc: %#18llx\n", frame.err_code);
}

void dump_memory(mx_handle_t proc, uintptr_t start, uint32_t len) {
    auto buf = static_cast<uint8_t*>(malloc(len));
    auto res = mx_debug_read_memory(proc, start, len, buf);
    if (res < 0) {
        printf("failed reading %p memory; error : %ld\n", (void*)start, res);
    } else if (res != 0) {
        hexdump(buf, (uint32_t)res);
    }
    free(buf);
}

void process_report(const mx_exception_report_t* report) {
    auto context = report->context;
    printf("<== fatal exception: process [%llu] thread [%llu]\n", context.pid, context.tid);
    printf("<== %s , PC at 0x%lx\n", exc_type_to_str(context.arch.subtype), context.arch.pc);

    auto process = mx_debug_get_process(0, context.pid);
    if (process <= 0) {
        printf("failed to get a handle to [%llu] : error %d\n", context.pid, process);
        return;
    }

    if (context.arch_id == ARCH_ID_X86_64) {
#if defined(__x86_64__)
        output_frame_x86_64(context.arch.u.x86_64);
        printf("bottom of user stack:\n");
        dump_memory(process, context.arch.u.x86_64.user_sp, 256u);
        printf("backtrace:\n");
        backtrace(process, context.arch.u.x86_64.ip, context.arch.u.x86_64.rbp, true);
#endif
    } else {
        // TODO: support other architectures.
        printf("unsuported architecture .. coming soon.\n");
    }
}

int main(int argc, char** argv) {
    mx_handle_t ex_port = mx_port_create(0u);
    if (ex_port < 0) {
        print_error(__LINE__);
        return 1;
    }

    mx_status_t status = mx_set_system_exception_port(ex_port, kSysExceptionKey, 0);
    if (status < 0) {
        print_error(__LINE__, "unable to set exception port");
        return 1;
    }

    printf("crashlogger service ready\n");

    while (true) {
        mx_exception_packet_t packet;
        mx_port_wait(ex_port, &packet, sizeof(packet));
        if (packet.hdr.key != kSysExceptionKey) {
            print_error(__LINE__, "invalid crash key");
            return 1;
        }

        process_report(&packet.report);
    }

    return 0;
}
