// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/exception.h>

#include <arch.h>
#include <err.h>
#include <stdio.h>
#include <trace.h>

#include <magenta/process_dispatcher.h>
#include <magenta/user_thread.h>

#define LOCAL_TRACE 0
#define TRACE_EXCEPTIONS 1

static const char* exception_type_to_string(uint type) {
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

static void build_exception_context(mx_exception_context_t* context,
                                    uint exception_type,
                                    arch_exception_context_t* arch_context) {
    context->type = exception_type;
    // TODO(dje): add more stuff
}

static void build_exception_report(mx_exception_report_t* report,
                                   UserThread* thread,
                                   uint exception_type,
                                   arch_exception_context_t* context,
                                   ulong ip) {
    memset(report, 0, sizeof(*report));
    report->pid = thread->process()->get_koid();
    report->tid = thread->get_koid();
    report->pc = ip;
    build_exception_context(&report->context, exception_type, context);
}

static status_t recv_exception_result(UserThread* thread,
                                      utils::RefPtr<Dispatcher> dispatcher,
                                      const mx_exception_report_t* report) {
    LTRACE_ENTRY;
    return thread->WaitForExceptionHandler(dispatcher, report);
}

static status_t try_exception_handler(utils::RefPtr<Dispatcher> dispatcher,
                                      UserThread* thread, uint exception_type,
                                      arch_exception_context_t* context, mx_vaddr_t ip) {
    if (!dispatcher)
        return ERR_NOT_FOUND;

    mx_exception_report_t report;
    build_exception_report(&report, thread, exception_type, context, ip);

    status_t status = recv_exception_result(thread, dispatcher, &report);
    LTRACEF("recv_exception_result returned %d\n", status);
    return status;
}

static status_t try_thread_exception_handler(UserThread* thread,
                                             uint exception_type,
                                             arch_exception_context_t* context,
                                             mx_vaddr_t ip) {
    return try_exception_handler(thread->exception_handler(), thread,
                                 exception_type, context, ip);
}

static status_t try_process_exception_handler(UserThread* thread,
                                              uint exception_type,
                                              arch_exception_context_t* context,
                                              mx_vaddr_t ip) {
    return try_exception_handler(thread->process()->exception_handler(), thread,
                                 exception_type, context, ip);
}

static status_t try_system_exception_handler(UserThread* thread,
                                             uint exception_type,
                                             arch_exception_context_t* context,
                                             mx_vaddr_t ip) {
    return try_exception_handler(GetSystemExceptionHandler(), thread,
                                 exception_type, context, ip);
}

// exception handler from low level architecturally-specific code
//
// If we return NO_ERROR, the caller is expected to resume the thread "as if"
// nothing happened, the handler is expected to have modified state such that
// resumption is possible.
// Otherwise, if we return, the result is ERR_NOT_VALID meaning the thread is
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
        return ERR_NOT_VALID;
    }
    auto process = thread->process();

    status_t status;

    status = try_thread_exception_handler(thread, exception_type, context, ip);
    if (status == NO_ERROR)
        return NO_ERROR;

    status = try_process_exception_handler(thread, exception_type, context, ip);
    if (status == NO_ERROR)
        return NO_ERROR;

    status = try_system_exception_handler(thread, exception_type, context, ip);
    if (status == NO_ERROR)
        return NO_ERROR;

#if TRACE_EXCEPTIONS
    printf("KERN: %s in magenta thread '%s' in process '%s' at IP 0x%lx\n",
           exception_type_to_string(exception_type), thread->name().data(),
           process->name().data(), ip);

    arch_dump_exception_context(context);
#endif

    // kill our process
    process->Kill();

    // exit
    thread->Exit();

    // should not get here
    panic("arch_exception_handler: fell out of thread exit somehow!\n");

    return ERR_NOT_SUPPORTED;
}
