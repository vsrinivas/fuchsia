// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch.h>
#include <arch/exception.h>
#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <stdio.h>
#include <trace.h>

#include <object/excp_port.h>
#include <object/job_dispatcher.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>

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
    case MX_EXCP_POLICY_ERROR:
        return "policy error";
    default:
        return "unknown fault";
    }
}

// This isn't an "iterator" in the pure c++ sense. We don't need all that
// complexity. I just couldn't think of a better term.
//
// Exception ports are tried in the following order:
// - debugger
// - thread
// - process
// - job (first owning job, then its parent job, and so on up to root job)
// - system
class ExceptionPortIterator final {
public:
    explicit ExceptionPortIterator(ThreadDispatcher* thread)
      : thread_(thread),
        previous_type_(ExceptionPort::Type::NONE) {
    }

    // Returns true with |out_eport| filled in for the next one to try.
    // Returns false if there are no more to try.
    bool Next(fbl::RefPtr<ExceptionPort>* out_eport) {
        fbl::RefPtr<ExceptionPort> eport;
        ExceptionPort::Type expected_type = ExceptionPort::Type::NONE;

        while (true) {
            switch (previous_type_) {
                case ExceptionPort::Type::NONE:
                    eport = thread_->process()->debugger_exception_port();
                    expected_type = ExceptionPort::Type::DEBUGGER;
                    break;
                case ExceptionPort::Type::DEBUGGER:
                    eport = thread_->exception_port();
                    expected_type = ExceptionPort::Type::THREAD;
                    break;
                case ExceptionPort::Type::THREAD:
                    eport = thread_->process()->exception_port();
                    expected_type = ExceptionPort::Type::PROCESS;
                    break;
                case ExceptionPort::Type::PROCESS:
                    previous_job_ = thread_->process()->job();
                    eport = previous_job_->exception_port();
                    expected_type = ExceptionPort::Type::JOB;
                    break;
                case ExceptionPort::Type::JOB:
                    previous_job_ = previous_job_->parent();
                    if (previous_job_) {
                        eport = previous_job_->exception_port();
                        expected_type = ExceptionPort::Type::JOB;
                    } else {
                        // Reached the root job and there was no handler.
                       return false;
                    }
                    break;
                default:
                    ASSERT_MSG(0, "unexpected exception type %d",
                               static_cast<int>(previous_type_));
                    __UNREACHABLE;
            }
            previous_type_ = expected_type;
            if (eport) {
                DEBUG_ASSERT(eport->type() == expected_type);
                *out_eport = fbl::move(eport);
                return true;
            }
        }
        __UNREACHABLE;
    }

private:
    ThreadDispatcher* thread_;
    ExceptionPort::Type previous_type_;
    // Jobs are traversed up their hierarchy. This is the previous one.
    fbl::RefPtr<JobDispatcher> previous_job_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(ExceptionPortIterator);
};

static mx_status_t try_exception_handler(fbl::RefPtr<ExceptionPort> eport,
                                         ThreadDispatcher* thread,
                                         const mx_exception_report_t* report,
                                         const arch_exception_context_t* arch_context,
                                         ThreadDispatcher::ExceptionStatus* estatus) {
    LTRACEF("Trying exception port type %d\n", static_cast<int>(eport->type()));
    auto status = thread->ExceptionHandlerExchange(eport, report, arch_context, estatus);
    LTRACEF("ExceptionHandlerExchange returned status %d, estatus %d\n", status, static_cast<int>(*estatus));

    return status;
}

enum handler_status_t {
    // thread is to be resumed
    HS_RESUME,
    // thread was killed
    HS_KILLED,
    // exception not handled (process will be killed)
    HS_NOT_HANDLED
};

// Subroutine of dispatch_user_exception to simplify the code.
// One useful thing this does is guarantee ExceptionPortIterator is properly
// destructed.
// |*out_processed| is set to a boolean indicating if at least one
// handler processed the exception.

static handler_status_t exception_handler_worker(uint exception_type,
                                                 arch_exception_context_t* context,
                                                 ThreadDispatcher* thread,
                                                 bool* out_processed) {
    *out_processed = false;

    mx_exception_report_t report;
    ExceptionPort::BuildArchReport(&report, exception_type, context);

    ExceptionPortIterator iter(thread);
    fbl::RefPtr<ExceptionPort> eport;

    while (iter.Next(&eport)) {
        // Initialize for paranoia's sake.
        ThreadDispatcher::ExceptionStatus estatus = ThreadDispatcher::ExceptionStatus::UNPROCESSED;
        auto status = try_exception_handler(eport, thread, &report, context, &estatus);
        LTRACEF("handler returned %d/%d\n",
                static_cast<int>(status), static_cast<int>(estatus));
        switch (status) {
        case MX_ERR_INTERNAL_INTR_KILLED:
            // thread was killed, probably with mx_task_kill
            return HS_KILLED;
        case MX_OK:
            switch (estatus) {
            case ThreadDispatcher::ExceptionStatus::TRY_NEXT:
                *out_processed = true;
                break;
            case ThreadDispatcher::ExceptionStatus::RESUME:
                return HS_RESUME;
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

    return HS_NOT_HANDLED;
}

// Dispatches an exception to the appropriate handler. Called by arch code
// when it cannot handle an exception.
//
// If we return MX_OK, the caller is expected to resume the thread "as if"
// nothing happened, the handler is expected to have modified state such that
// resumption is possible.
//
// If we return MX_ERR_BAD_STATE, the current thread is not a user thread
// (i.e., not associated with a ThreadDispatcher).
//
// Otherwise, we cause the current thread to exit and do not return at all.
//
// TODO(dje): Support unwinding from this exception and introducing a different
// exception?
status_t dispatch_user_exception(uint exception_type,
                                 arch_exception_context_t* context) {
    LTRACEF("type %u, context %p\n", exception_type, context);

    ThreadDispatcher* thread = ThreadDispatcher::GetCurrent();
    if (unlikely(!thread)) {
        // The current thread is not a user thread; bail.
        return MX_ERR_BAD_STATE;
    }

    bool processed;
    handler_status_t hstatus = exception_handler_worker(exception_type, context,
                                                        thread, &processed);
    switch (hstatus) {
        case HS_RESUME:
            return MX_OK;
        case HS_KILLED:
            thread->Exit();
            __UNREACHABLE;
        case HS_NOT_HANDLED:
            break;
        default:
            ASSERT_MSG(0, "unexpected exception worker result %d", static_cast<int>(hstatus));
            __UNREACHABLE;
    }

    auto process = thread->process();

#if TRACE_EXCEPTIONS
    if (!processed) {
        // only print this if an exception handler wasn't involved
        // in handling the exception
        char pname[MX_MAX_NAME_LEN];
        process->get_name(pname);
        char tname[MX_MAX_NAME_LEN];
        thread->get_name(tname);
        printf("KERN: %s in user thread '%s' in process '%s'\n",
               excp_type_to_string(exception_type), tname, pname);

        arch_dump_exception_context(context);
    }
#endif

    // kill our process
    process->Kill();

    // exit
    thread->Exit();

    // should not get here
    panic("fell out of thread exit somehow!\n");
    __UNREACHABLE;
}
