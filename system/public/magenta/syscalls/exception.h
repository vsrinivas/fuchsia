// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/syscalls/types.h>

__BEGIN_CDECLS

// ask clang format not to mess up the indentation:
// clang-format off

// The kind of an exception.
typedef enum {
    // These are architectural exceptions.
    // Further information can be found in report.context.arch.

    // General exception not covered by another value.
    MX_EXCP_GENERAL = 0,
    MX_EXCP_FATAL_PAGE_FAULT = 1,
    MX_EXCP_UNDEFINED_INSTRUCTION = 2,
    MX_EXCP_SW_BREAKPOINT = 3,
    MX_EXCP_HW_BREAKPOINT = 4,
    MX_EXCP_UNALIGNED_ACCESS = 5,

    MX_EXCP_MAX_ARCH = 99,

    // Synthetic exceptions.

    // A thread has started.
    MX_EXCP_START = 100,

    // A thread or process has exited or otherwise terminated.
    // N.B. "gone" notifications are not responded to.
    MX_EXCP_GONE = 101,
} mx_excp_type_t;

#define MX_EXCP_IS_ARCH(excp) ((excp) <= MX_EXCP_MAX_ARCH)

typedef struct x86_64_exc_data {
    uint64_t vector;
    uint64_t err_code;
    uint64_t cr2;
} x86_64_exc_data_t;

typedef struct arm64_exc_data {
    uint64_t far;
} arm64_exc_data_t;

#define ARCH_ID_UNKNOWN        0u
#define ARCH_ID_X86_64         1u
#define ARCH_ID_ARM_64         2u

// data associated with an exception (siginfo in linux parlance)
typedef struct mx_exception_context {
    // One of ARCH_ID above.
    uint32_t arch_id;
    // The process of the thread with the exception.
    mx_koid_t pid;

    // The thread that got the exception.
    // This is zero in "process gone" notifications.
    mx_koid_t tid;

    struct {
        mx_vaddr_t pc;
        union {
            x86_64_exc_data_t x86_64;
            arm64_exc_data_t  arm_64;
        } u;
        // TODO(dje): add more stuff, revisit packing
        // For an example list of things one might add, see linux siginfo.
    } arch;
} mx_exception_context_t;

// The common header of all exception reports.
// TODO(dje): For now we assume all exceptions are thread-related.
// A reasonably safe assumption, but the intent is to not preclude
// other kinds of exceptions should a need ever arise.
typedef struct mx_exception_header {
    // The actual size, in bytes, of the report (including this field),
    // but *not* including mx_packet_header_t.
    uint32_t size;

    // While IWBN to use an enum here, it's still not portable in C.
    uint32_t /*mx_excp_type_t*/ type;
} mx_exception_header_t;

// Data reported to an exception handler for most exceptions.
typedef struct mx_exception_report {
    mx_exception_header_t header;
    // The remainder of the report is exception-specific.
    // TODO(dje): For now we KISS and use the same struct for everything.
    mx_exception_context_t context;
} mx_exception_report_t;

// The status argument to _magenta_mark_exception_handled.
// Negative values are for internal use only.
typedef enum {
    MX_EXCEPTION_STATUS_HANDLER_GONE = -2,
    MX_EXCEPTION_STATUS_WAITING = -1,
    // As an analogy, this would be like typing "c" in gdb after a segfault.
    // In linux the signal would be delivered to the thread, which would
    // either terminate the process or run a signal handler if defined.
    // In magenta this gives the next signal handler in the list a crack at
    // the exception.
    MX_EXCEPTION_STATUS_NOT_HANDLED = 0,
    // As an analogy, this would be like typing "sig 0" in gdb after a
    // segfault. The faulting instruction will be retried. If, for example, it
    // segfaults again then the user is back in the debugger again, which is
    // working as intended.
    // Note: We don't, currently at least, support delivering a different
    // exception (signal in linux parlance) to the thread. As an analogy, this
    // would be like typing "sig 8" in gdb after getting a segfault (which is
    // signal 11).
    MX_EXCEPTION_STATUS_RESUME = 1
} mx_exception_status_t;

// Flags for mx_task_resume()
#define MX_RESUME_EXCEPTION (1)
// Indicates that we should resume the thread from stopped-in-exception state
// (default resume does not do so)

#define MX_RESUME_NOT_HANDLED (2)
// Only meaningful when combined with MX_RESUME_EXCEPTION
// Indicates that instead of resuming from the faulting instruction we instead
// let any additional exception handlers (eg, system after process) take a shot
// at it, and if there are no additional handlers, the thread will terminate

// Flags for mx_object_bind_exception_port.
#define MX_EXCEPTION_PORT_DEBUGGER (1)
// When binding an exception port to a process, set the process's debugger
// exception port.

__END_CDECLS
