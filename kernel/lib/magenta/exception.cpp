// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/exception.h>

#include <arch.h>
#include <assert.h>
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
    case MX_EXCP_UNALIGNED_ACCESS:
        return "alignment fault";
    default:
        return "unknown fault";
    }
}

static void build_exception_report(mx_exception_report_t* report,
                                   uint exception_type,
                                   const arch_exception_context_t* arch_context) {
    // TODO(dje): Move to ExceptionPort::BuildArchExceptionReport.
    memset(report, 0, sizeof(*report));
    // TODO(dje): wip, just make all reports the same maximum size for now
    report->header.size = sizeof(*report);
    report->header.type = exception_type;
    arch_fill_in_exception_context(arch_context, report);
}

static status_t try_exception_handler(mxtl::RefPtr<ExceptionPort> eport,
                                      ExceptionPort::Type expected_type,
                                      UserThread* thread,
                                      const mx_exception_report_t* report,
                                      const arch_exception_context_t* arch_context,
                                      UserThread::ExceptionStatus* estatus) {
    if (!eport)
        return MX_ERR_NOT_FOUND;

    DEBUG_ASSERT(eport->type() == expected_type);
    auto status = thread->ExceptionHandlerExchange(eport, report, arch_context, estatus);
    LTRACEF("ExceptionHandlerExchange returned status %d, estatus %d\n", status, static_cast<int>(*estatus));
    return status;
}

static status_t try_debugger_exception_handler(UserThread* thread,
                                               const mx_exception_report_t* report,
                                               const arch_exception_context_t* arch_context,
                                               UserThread::ExceptionStatus* estatus) {
    LTRACE_ENTRY;
    mxtl::RefPtr<ExceptionPort> eport = thread->process()->debugger_exception_port();
    return try_exception_handler(eport, ExceptionPort::Type::DEBUGGER,
                                 thread, report, arch_context, estatus);
}

static status_t try_thread_exception_handler(UserThread* thread,
                                             const mx_exception_report_t* report,
                                             const arch_exception_context_t* arch_context,
                                             UserThread::ExceptionStatus* estatus) {
    LTRACE_ENTRY;
    mxtl::RefPtr<ExceptionPort> eport = thread->exception_port();
    return try_exception_handler(eport, ExceptionPort::Type::THREAD,
                                 thread, report, arch_context, estatus);
}

static status_t try_process_exception_handler(UserThread* thread,
                                              const mx_exception_report_t* report,
                                              const arch_exception_context_t* arch_context,
                                              UserThread::ExceptionStatus* estatus) {
    LTRACE_ENTRY;
    mxtl::RefPtr<ExceptionPort> eport = thread->process()->exception_port();
    return try_exception_handler(eport, ExceptionPort::Type::PROCESS,
                                 thread, report, arch_context, estatus);
}

static status_t try_system_exception_handler(UserThread* thread,
                                             const mx_exception_report_t* report,
                                             const arch_exception_context_t* arch_context,
                                             UserThread::ExceptionStatus* estatus) {
    LTRACE_ENTRY;
    mxtl::RefPtr<ExceptionPort> eport = GetSystemExceptionPort();
    return try_exception_handler(eport, ExceptionPort::Type::SYSTEM,
                                 thread, report, arch_context, estatus);
}

// exception handler from low level architecturally-specific code
//
// If we return MX_OK, the caller is expected to resume the thread "as if"
// nothing happened, the handler is expected to have modified state such that
// resumption is possible.
// Otherwise, if we return, the result is MX_ERR_BAD_STATE meaning the thread is
// not a magenta thread.
//
// TODO(dje): Support unwinding from this exception and introducing a
// different exception?

status_t magenta_exception_handler(uint exception_type,
                                   arch_exception_context_t* context) {
    LTRACEF("type %u, context %p\n", exception_type, context);

    UserThread* thread = UserThread::GetCurrent();
    if (unlikely(!thread)) {
        // we're not in magenta thread context, bail
        return MX_ERR_BAD_STATE;
    }

    typedef status_t (Handler)(UserThread* thread,
                               const mx_exception_report_t* report,
                               const arch_exception_context_t* arch_context,
                               UserThread::ExceptionStatus* estatus);

    static Handler* const handlers[] = {
        try_debugger_exception_handler,
        try_thread_exception_handler,
        try_process_exception_handler,
        try_system_exception_handler
    };

    bool processed = false;
    mx_exception_report_t report;
    build_exception_report(&report, exception_type, context);

    for (size_t i = 0; i < countof(handlers); ++i) {
        // Initialize for paranoia's sake.
        UserThread::ExceptionStatus estatus = UserThread::ExceptionStatus::UNPROCESSED;
        auto status = handlers[i](thread, &report, context, &estatus);
        LTRACEF("handler returned %d/%d\n",
                static_cast<int>(status), static_cast<int>(estatus));
        switch (status) {
        case MX_ERR_INTERNAL_INTR_KILLED:
            // thread was killed, probably with mx_task_kill
            thread->Exit();
            __UNREACHABLE;
        case MX_ERR_NOT_FOUND:
            continue;
        case MX_OK:
            switch (estatus) {
            case UserThread::ExceptionStatus::TRY_NEXT:
                processed = true;
                break;
            case UserThread::ExceptionStatus::RESUME:
                return MX_OK;
            default:
                ASSERT_MSG(0, "invalid exception status %d",
                           static_cast<int>(estatus));
                __UNREACHABLE;
            }
            break;
        default:
            ASSERT_MSG(0, "unexpected exception result %d", status);
            __UNREACHABLE;
        }
    }

    auto process = thread->process();

#if TRACE_EXCEPTIONS
    if (!processed) {
        // only print this if an exception handler wasn't involved
        // in handling the exception
        char pname[MX_MAX_NAME_LEN];
        process->get_name(pname);
        printf("KERN: %s in magenta thread '%s' in process '%s'\n",
               excp_type_to_string(exception_type), thread->name(), pname);

        arch_dump_exception_context(context);
    }
#endif

    // kill our process
    process->Kill();

    // exit
    thread->Exit();

    // should not get here
    panic("arch_exception_handler: fell out of thread exit somehow!\n");

    return MX_ERR_NOT_SUPPORTED;
}
