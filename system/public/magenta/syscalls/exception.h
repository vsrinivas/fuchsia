// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <magenta/syscalls/port.h>

__BEGIN_CDECLS

// ask clang format not to mess up the indentation:
// clang-format off

// This bit is set for synthetic exceptions to distinguish them from
// architectural exceptions.
// Note: Port packet types provide 8 bits to distinguish the exception type.
// See magenta/port.h.
#define MX_EXCP_SYNTH 0x80

// The kind of an exception.
// Exception types are a subset of port packet types. See magenta/port.h.
typedef enum {
    // These are architectural exceptions.
    // Depending on the exception, further information can be found in
    // |report.context.arch|.

    // General exception not covered by another value.
    MX_EXCP_GENERAL = MX_PKT_TYPE_EXCEPTION(0),
    MX_EXCP_FATAL_PAGE_FAULT = MX_PKT_TYPE_EXCEPTION(1),
    MX_EXCP_UNDEFINED_INSTRUCTION = MX_PKT_TYPE_EXCEPTION(2),
    MX_EXCP_SW_BREAKPOINT = MX_PKT_TYPE_EXCEPTION(3),
    MX_EXCP_HW_BREAKPOINT = MX_PKT_TYPE_EXCEPTION(4),
    MX_EXCP_UNALIGNED_ACCESS = MX_PKT_TYPE_EXCEPTION(5),

    // Synthetic exceptions.

    // A thread is starting.
    // This exception is sent to debuggers only (MX_EXCEPTION_PORT_DEBUGGER).
    // The thread is paused until it is resumed by the debugger
    // with mx_task_resume.
    MX_EXCP_THREAD_STARTING = MX_PKT_TYPE_EXCEPTION(MX_EXCP_SYNTH | 0),

    // A thread has suspended.
    // This exception is sent to debuggers only (MX_EXCEPTION_PORT_DEBUGGER).
    // The thread is paused until it is resumed by the debugger
    // with mx_task_resume. This resume is different though: it's not resuming
    // from an exception, so don't pass MX_RESUME_EXCEPTION, pass 0.
    // A note on the word tense here: This is named "suspended" and not
    // "suspending" because the thread has completely suspended at this point.
    // N.B. This notification is not replied to.
    MX_EXCP_THREAD_SUSPENDED = MX_PKT_TYPE_EXCEPTION(MX_EXCP_SYNTH | 1),

    // A thread has resumed after being suspended.
    // This exception is sent to debuggers only (MX_EXCEPTION_PORT_DEBUGGER).
    // This is the counterpart to MX_EXCP_THREAD_SUSPENDED.
    // A note on the word tense here: This is named "resumed" and not
    // "resuming" because the thread has completely resumed at this point.
    // N.B. This notification is not replied to.
    MX_EXCP_THREAD_RESUMED = MX_PKT_TYPE_EXCEPTION(MX_EXCP_SYNTH | 2),

    // A thread is exiting.
    // This exception is sent to debuggers only (MX_EXCEPTION_PORT_DEBUGGER).
    // This exception is different from MX_EXCP_GONE in that a debugger can
    // still examine thread state.
    // The thread is paused until it is resumed by the debugger
    // with mx_task_resume.
    MX_EXCP_THREAD_EXITING = MX_PKT_TYPE_EXCEPTION(MX_EXCP_SYNTH | 3),

    // A thread or process has exited or otherwise terminated.
    // At this point thread/process state is no longer available.
    // Process gone notifications are only sent to the process exception port
    // or debugger exception port (if one is registered).
    // Thread gone notifications are only sent to the thread exception port
    // (if one is registered).
    // N.B. This notification is not replied to.
    MX_EXCP_GONE = MX_PKT_TYPE_EXCEPTION(MX_EXCP_SYNTH | 4),

    // This exception is generated when a syscall fails with a job policy
    // error (for example, an invalid handle argument is passed to the
    // syscall when the MX_POL_BAD_HANDLE policy is enabled) and
    // MX_POL_ACTION_EXCEPTION is set for the policy.  The thread that
    // invoked the syscall may be resumed with mx_task_resume().
    MX_EXCP_POLICY_ERROR = MX_PKT_TYPE_EXCEPTION(MX_EXCP_SYNTH | 5),
} mx_excp_type_t;

// Assuming |excp| is an exception type, return non-zero if it is an
// architectural exception.
#define MX_EXCP_IS_ARCH(excp) \
  (((excp) & (MX_PKT_TYPE_EXCEPTION(MX_EXCP_SYNTH) & ~MX_PKT_TYPE_MASK)) == 0)

typedef struct x86_64_exc_data {
    uint64_t vector;
    uint64_t err_code;
    uint64_t cr2;
} x86_64_exc_data_t;

typedef struct arm64_exc_data {
    uint32_t esr;
    uint64_t far;
} arm64_exc_data_t;

// data associated with an exception (siginfo in linux parlance)
// Things available from regsets (e.g., pc) are not included here.
// For an example list of things one might add, see linux siginfo.
typedef struct mx_exception_context {
    struct {
        union {
            x86_64_exc_data_t x86_64;
            arm64_exc_data_t  arm_64;
        } u;
    } arch;
} mx_exception_context_t;

// The common header of all exception reports.
typedef struct mx_exception_header {
    // The actual size, in bytes, of the report (including this field).
    uint32_t size;

    // While IWBN to use an enum here, it's still not portable in C.
    uint32_t /*mx_excp_type_t*/ type;
} mx_exception_header_t;

// Data reported to an exception handler for most exceptions.
typedef struct mx_exception_report {
    mx_exception_header_t header;
    // The remainder of the report is exception-specific.
    mx_exception_context_t context;
} mx_exception_report_t;

// Options for mx_task_resume()
#define MX_RESUME_EXCEPTION (1)
// Indicates that we should resume the thread from stopped-in-exception state
// (default resume does not do so)

#define MX_RESUME_TRY_NEXT (2)
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
#define MX_EXCEPTION_PORT_TYPE_JOB      (4u)
#define MX_EXCEPTION_PORT_TYPE_SYSTEM   (5u)

__END_CDECLS
