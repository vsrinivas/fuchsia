// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crashlog.h>

#include <ctype.h>
#include <inttypes.h>
#include <kernel/thread.h>
#include <lib/version.h>
#include <platform.h>
#include <printf.h>
#include <string.h>

crashlog_t crashlog = {};

size_t crashlog_to_string(char* out, const size_t out_len) {
    char* buf = out;
    size_t remain = out_len;
    size_t len;

    buf[0] = '\0';

    len = snprintf(buf, remain, "ZIRCON KERNEL PANIC\n\n");
    if (len > remain) {
        return out_len;
    }
    remain -= len;
    buf += len;

    len = snprintf(buf, remain, "UPTIME (ms)\n%" PRIu64 "\n\n", current_time() / ZX_MSEC(1));
    if (len > remain) {
        return out_len;
    }
    remain -= len;
    buf += len;

    len = snprintf(buf, remain, "VERSION\nbuild_id %s\nelf_build_id %s\n\n", version.buildid, version.elf_build_id);
    if (len > remain) {
        return out_len;
    }
    remain -= len;
    buf += len;

    len = snprintf(buf, remain, "BASE ADDRESS\n%#lx\n\n", crashlog.base_address);
    if (len > remain) {
        return out_len;
    }
    remain -= len;
    buf += len;

#if defined(__aarch64__)
    len = snprintf(buf, remain, "REGISTERS\n"
                                "  x0 %#18" PRIx64 "\n"
                                "  x1 %#18" PRIx64 "\n"
                                "  x2 %#18" PRIx64 "\n"
                                "  x3 %#18" PRIx64 "\n"
                                "  x4 %#18" PRIx64 "\n"
                                "  x5 %#18" PRIx64 "\n"
                                "  x6 %#18" PRIx64 "\n"
                                "  x7 %#18" PRIx64 "\n"
                                "  x8 %#18" PRIx64 "\n"
                                "  x9 %#18" PRIx64 "\n"
                                " x10 %#18" PRIx64 "\n"
                                " x11 %#18" PRIx64 "\n"
                                " x12 %#18" PRIx64 "\n"
                                " x13 %#18" PRIx64 "\n"
                                " x14 %#18" PRIx64 "\n"
                                " x15 %#18" PRIx64 "\n"
                                " x16 %#18" PRIx64 "\n"
                                " x17 %#18" PRIx64 "\n"
                                " x18 %#18" PRIx64 "\n"
                                " x19 %#18" PRIx64 "\n"
                                " x20 %#18" PRIx64 "\n"
                                " x21 %#18" PRIx64 "\n"
                                " x22 %#18" PRIx64 "\n"
                                " x23 %#18" PRIx64 "\n"
                                " x24 %#18" PRIx64 "\n"
                                " x25 %#18" PRIx64 "\n"
                                " x26 %#18" PRIx64 "\n"
                                " x27 %#18" PRIx64 "\n"
                                " x28 %#18" PRIx64 "\n"
                                " x29 %#18" PRIx64 "\n"
                                " lr  %#18" PRIx64 "\n"
                                " usp %#18" PRIx64 "\n"
                                " elr %#18" PRIx64 "\n"
                                "spsr %#18" PRIx64 "\n"
                                "\n",
                   crashlog.iframe->r[0],
                   crashlog.iframe->r[1],
                   crashlog.iframe->r[2],
                   crashlog.iframe->r[3],
                   crashlog.iframe->r[4],
                   crashlog.iframe->r[5],
                   crashlog.iframe->r[6],
                   crashlog.iframe->r[7],
                   crashlog.iframe->r[8],
                   crashlog.iframe->r[9],
                   crashlog.iframe->r[10],
                   crashlog.iframe->r[11],
                   crashlog.iframe->r[12],
                   crashlog.iframe->r[13],
                   crashlog.iframe->r[14],
                   crashlog.iframe->r[15],
                   crashlog.iframe->r[16],
                   crashlog.iframe->r[17],
                   crashlog.iframe->r[18],
                   crashlog.iframe->r[19],
                   crashlog.iframe->r[20],
                   crashlog.iframe->r[21],
                   crashlog.iframe->r[22],
                   crashlog.iframe->r[23],
                   crashlog.iframe->r[24],
                   crashlog.iframe->r[25],
                   crashlog.iframe->r[26],
                   crashlog.iframe->r[27],
                   crashlog.iframe->r[28],
                   crashlog.iframe->r[29],
                   crashlog.iframe->lr,
                   crashlog.iframe->usp,
                   crashlog.iframe->elr,
                   crashlog.iframe->spsr);
    if (len > remain) {
        return out_len;
    }
    remain -= len;
    buf += len;
#elif defined(__x86_64__)
    len = snprintf(buf, remain, "REGISTERS\n"
                                "  CS %#18" PRIx64 "\n"
                                " RIP %#18" PRIx64 "\n"
                                " EFL %#18" PRIx64 "\n"
                                " CR2 %#18lx\n"
                                " RAX %#18" PRIx64 "\n"
                                " RBX %#18" PRIx64 "\n"
                                " RCX %#18" PRIx64 "\n"
                                " RDX %#18" PRIx64 "\n"
                                " RSI %#18" PRIx64 "\n"
                                " RDI %#18" PRIx64 "\n"
                                " RBP %#18" PRIx64 "\n"
                                " RSP %#18" PRIx64 "\n"
                                "  R8 %#18" PRIx64 "\n"
                                "  R9 %#18" PRIx64 "\n"
                                " R10 %#18" PRIx64 "\n"
                                " R11 %#18" PRIx64 "\n"
                                " R12 %#18" PRIx64 "\n"
                                " R13 %#18" PRIx64 "\n"
                                " R14 %#18" PRIx64 "\n"
                                " R15 %#18" PRIx64 "\n"
                                "errc %#18" PRIx64 "\n"
                                "\n",
                   crashlog.iframe->cs,
                   crashlog.iframe->ip,
                   crashlog.iframe->flags,
                   x86_get_cr2(),
                   crashlog.iframe->rax,
                   crashlog.iframe->rbx,
                   crashlog.iframe->rcx,
                   crashlog.iframe->rdx,
                   crashlog.iframe->rsi,
                   crashlog.iframe->rdi,
                   crashlog.iframe->rbp,
                   crashlog.iframe->user_sp,
                   crashlog.iframe->r8,
                   crashlog.iframe->r9,
                   crashlog.iframe->r10,
                   crashlog.iframe->r11,
                   crashlog.iframe->r12,
                   crashlog.iframe->r13,
                   crashlog.iframe->r14,
                   crashlog.iframe->r15,
                   crashlog.iframe->err_code);
    if (len > remain) {
        return out_len;
    }
    remain -= len;
    buf += len;
#endif

    len = snprintf(buf, remain, "BACKTRACE (up to 16 calls)\n");
    if (len > remain) {
        return out_len;
    }
    remain -= len;
    buf += len;

    len = thread_append_current_backtrace(buf, remain);
    if (len > remain) {
        return out_len;
    }
    remain -= len;
    buf += len;

    len = snprintf(buf, remain, "\n");
    if (len > remain) {
        return out_len;
    }
    remain -= len;
    buf += len;

    return out_len - remain;
}
