// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>

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

    // A thread is starting.
    // This exception is sent to debuggers only (MX_EXCEPTION_PORT_DEBUGGER).
    // The thread is paused until it is resumed by the debugger
    // with mx_task_resume.
    MX_EXCP_THREAD_STARTING = 100,

    // A thread has suspended.
    // This exception is sent to debuggers only (MX_EXCEPTION_PORT_DEBUGGER).
    // The thread is paused until it is resumed by the debugger
    // with mx_task_resume. This resume is different though: it's not resuming
    // from an exception, so don't pass MX_RESUME_EXCEPTION, pass 0.
    // A note on the word tense here: This is named "suspended" and not
    // "suspending" because the thread has completely suspended at this point.
    // N.B. This notification is not replied to.
    MX_EXCP_THREAD_SUSPENDED = 101,

    // A thread has resumed after being suspended.
    // This exception is sent to debuggers only (MX_EXCEPTION_PORT_DEBUGGER).
    // This is the counterpart to MX_EXCP_THREAD_SUSPENDED.
    // A note on the word tense here: This is named "resumed" and not
    // "resuming" because the thread has completely resumed at this point.
    // N.B. This notification is not replied to.
    MX_EXCP_THREAD_RESUMED = 102,

    // A thread is exiting.
    // This exception is sent to debuggers only (MX_EXCEPTION_PORT_DEBUGGER).
    // This exception is different from MX_EXCP_GONE in that a debugger can
    // still examine thread state.
    // The thread is paused until it is resumed by the debugger
    // with mx_task_resume.
    MX_EXCP_THREAD_EXITING = 103,

    // A thread or process has exited or otherwise terminated.
    // At this point thread/process state is no longer available.
    // Process gone notifications are only sent to the process exception port
    // or debugger exception port (if one is registered).
    // Thread gone notifications are only sent to the thread exception port
    // (if one is registered).
    // N.B. This notification is not replied to.
    MX_EXCP_GONE = 104,
} mx_excp_type_t;

#define MX_EXCP_IS_ARCH(excp) ((excp) <= MX_EXCP_MAX_ARCH)

typedef struct x86_64_exc_data {
    uint64_t vector;
    uint64_t err_code;
    uint64_t cr2;
} x86_64_exc_data_t;

typedef struct arm64_exc_data {
    uint32_t esr;
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

// Options for mx_task_resume()
#define MX_RESUME_EXCEPTION (1)
// Indicates that we should resume the thread from stopped-in-exception state
// (default resume does not do so)

#define MX_RESUME_TRY_NEXT (2)
#define MX_RESUME_NOT_HANDLED (2) // deprecated
// Only meaningful when combined with MX_RESUME_EXCEPTION
// Indicates that instead of resuming from the faulting instruction we instead
// let the next exception handler in the search order, if any, process the
// exception. If there are no more then the entire process is killed.

// Options for mx_task_bind_exception_port.
#define MX_EXCEPTION_PORT_DEBUGGER (1)
// When binding an exception port to a process, set the process's debugger
// exception port.
#define MX_EXCEPTION_PORT_UNBIND_QUIETLY (2)
// When unbinding an exception port from a thread or process, any threads that
// got an exception and are waiting for a response from this exception port
// will continue to wait for a response.

// The type of exception port a thread may be waiting for a response from.
// These values are reported in mx_info_thread_t.wait_exception_port_type.
#define MX_EXCEPTION_PORT_TYPE_NONE     (0u)
#define MX_EXCEPTION_PORT_TYPE_DEBUGGER (1u)
#define MX_EXCEPTION_PORT_TYPE_THREAD   (2u)
#define MX_EXCEPTION_PORT_TYPE_PROCESS  (3u)
#define MX_EXCEPTION_PORT_TYPE_SYSTEM   (4u)

__END_CDECLS
