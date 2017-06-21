// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <sys/types.h>
#include <magenta/types.h>
#include <magenta/syscalls/exception.h>

__BEGIN_CDECLS

typedef struct arch_exception_context arch_exception_context_t;

status_t magenta_exception_handler(uint exception_type,
                                   struct arch_exception_context* context,
                                   mx_vaddr_t ip);

// This reports an exception that was raised by a syscall using
// thread_signal_exception().
status_t magenta_report_syscall_exception(void);

// arch code must implement this to dump the architecturally specific state they passed to magenta_exception_handler
void arch_dump_exception_context(const arch_exception_context_t *);

// request the arch code fill in the mx_exception_context report with arch specific information
void arch_fill_in_exception_context(const arch_exception_context_t *, mx_exception_report_t *);

// request the arch code fill in the mx_exception_context report with arch specific information
void arch_fill_in_suspension_context(mx_exception_report_t *);

__END_CDECLS
