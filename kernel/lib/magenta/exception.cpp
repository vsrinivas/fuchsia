// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/exception.h>

#include <arch.h>
#include <err.h>
#include <inttypes.h>
#include <stdio.h>
#include <trace.h>

#include <magenta/process_dispatcher.h>
#include <magenta/user_thread.h>

#define LOCAL_TRACE 0
#define TRACE_EXCEPTIONS 1

static const char* excp_type_to_string(uint type) {
    switch (type) {
    case MX_EXCP_FATAL_PAGE_FAULT:
        return "fatal page fault";
    case MX_EXCP_UNDEFINED_INSTRUCTION:
        return "undefined instruction";
    case MX_EXCP_GENERAL:
        return "general fault";
    case MX_EXCP_SW_BREAKPOINT:
        return "software breakpoint";
    case MX_EXCP_HW_BREAKPOINT:
        return "hardware breakpoint";
    default:
        return "unknown fault";
    }
}

static void build_arch_exception_context(mx_exception_report_t* report,
                                         ulong ip,
                                         const arch_exception_context_t* arch_context) {
    report->context.arch.pc = ip;

    arch_fill_in_exception_context(arch_context, report);
}

static void build_exception_report(mx_exception_report_t* report,
                                   UserThread* thread,
                                   uint exception_type,
                                   const arch_exception_context_t* arch_context,
                                   ulong ip) {
    memset(report, 0, sizeof(*report));
    // TODO(dje): wip, just make all reports the same maximum size for now
    report->header.size = sizeof(*report);
    report->header.type = exception_type;
    report->context.pid = thread->process()->get_koid();
    report->context.tid = thread->get_koid();
    build_arch_exception_context(report, ip, arch_context);
}

static status_t try_exception_handler(mxtl::RefPtr<ExceptionPort> eport, UserThread* thread,
                                      const mx_exception_report_t* report,
                                      const arch_exception_context_t* arch_context,
                                      bool* processed) {
    if (!eport)
        return ERR_NOT_FOUND;

    *processed = true;
    status_t status = thread->ExceptionHandlerExchange(eport, report, arch_context);
    LTRACEF("ExceptionHandlerExchange returned %d\n", status);
    return status;
}

static status_t try_debugger_exception_handler(UserThread* thread,
                                               const mx_exception_report_t* report,
                                               const arch_exception_context_t* arch_context,
                                               bool* processed) {
    LTRACE_ENTRY;
    return try_exception_handler(thread->process()->debugger_exception_port(), thread, report, arch_context, processed);
}

static status_t try_thread_exception_handler(UserThread* thread,
                                             const mx_exception_report_t* report,
                                             const arch_exception_context_t* arch_context,
                                             bool* processed) {
    LTRACE_ENTRY;
    return try_exception_handler(thread->exception_port(), thread, report, arch_context, processed);
}

static status_t try_process_exception_handler(UserThread* thread,
                                              const mx_exception_report_t* report,
                                              const arch_exception_context_t* arch_context,
                                              bool* processed) {
    LTRACE_ENTRY;
    return try_exception_handler(thread->process()->exception_port(), thread, report, arch_context, processed);
}

static status_t try_system_exception_handler(UserThread* thread,
                                             const mx_exception_report_t* report,
                                             const arch_exception_context_t* arch_context,
                                             bool* processed) {
    LTRACE_ENTRY;
    return try_exception_handler(GetSystemExceptionPort(), thread, report, arch_context, processed);
}

// exception handler from low level architecturally-specific code
//
// If we return NO_ERROR, the caller is expected to resume the thread "as if"
// nothing happened, the handler is expected to have modified state such that
// resumption is possible.
// Otherwise, if we return, the result is ERR_BAD_STATE meaning the thread is
// not a magenta thread.
//
// TODO(dje): Support unwinding from this exception and introducing a
// different exception.

status_t magenta_exception_handler(uint exception_type,
                                   arch_exception_context_t* context,
                                   mx_vaddr_t ip) {
    LTRACEF("type %u, context %p\n", exception_type, context);

    UserThread* thread = UserThread::GetCurrent();
    if (unlikely(!thread)) {
        // we're not in magenta thread context, bail
        return ERR_BAD_STATE;
    }

    bool processed = false;
    status_t status;
    mx_exception_report_t report;
    build_exception_report(&report, thread, exception_type, context, ip);

    status = try_debugger_exception_handler(thread, &report, context, &processed);
    if (status == NO_ERROR)
        return NO_ERROR;

    status = try_thread_exception_handler(thread, &report, context, &processed);
    if (status == NO_ERROR)
        return NO_ERROR;

    status = try_process_exception_handler(thread, &report, context, &processed);
    if (status == NO_ERROR)
        return NO_ERROR;

    status = try_system_exception_handler(thread, &report, context, &processed);
    if (status == NO_ERROR)
        return NO_ERROR;

    auto process = thread->process();

#if TRACE_EXCEPTIONS
    if (!processed) {
        // only print this if an exception handler wasn't involved
        // in handling the exception
        printf("KERN: %s in magenta thread '%s' in process '%s' at IP %#"
               PRIxPTR "\n",
               excp_type_to_string(exception_type), thread->name().data(),
               process->name().data(), ip);

        arch_dump_exception_context(context);
    }
#endif

    // kill our process
    process->Kill();

    // exit
    thread->Exit();

    // should not get here
    panic("arch_exception_handler: fell out of thread exit somehow!\n");

    return ERR_NOT_SUPPORTED;
}
