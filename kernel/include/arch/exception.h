// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <sys/types.h>

__BEGIN_CDECLS

typedef struct arch_exception_context arch_exception_context_t;
typedef struct mx_exception_report mx_exception_report_t;

// Called by arch code when it cannot handle an exception.
// |context| is architecture-specific, and can be dumped to the console
// using arch_dump_exception_context(). Implemented by non-arch code.
status_t dispatch_user_exception(uint exception_type,
                                 arch_exception_context_t* context);

// Dispatches an exception that was raised by a syscall using
// thread_signal_policy_exception() (see <kernel/thread.h>), causing
// dispatch_user_exception() to be called with the current context.
// Implemented by arch code.
status_t arch_dispatch_user_policy_exception(void);

// Dumps architecture-specific state to the console. |context| typically comes
// from a call to dispatch_user_exception(). Implemented by arch code.
void arch_dump_exception_context(const arch_exception_context_t* context);

// Sets |report| using architecture-specific information from |context|.
// Implemented by arch code.
void arch_fill_in_exception_context(
    const arch_exception_context_t* context, mx_exception_report_t* report);

__END_CDECLS
