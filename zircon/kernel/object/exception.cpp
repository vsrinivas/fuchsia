// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch.h>
#include <arch/exception.h>
#include <assert.h>
#include <err.h>
#include <fbl/auto_call.h>
#include <inttypes.h>
#include <stdio.h>
#include <trace.h>

#include <object/excp_port.h>
#include <object/job_dispatcher.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>

#include <fbl/auto_call.h>
#include <zircon/syscalls/object.h>

#define LOCAL_TRACE 0
#define TRACE_EXCEPTIONS 1

static const char* excp_type_to_string(uint type) {
    switch (type) {
    case ZX_EXCP_FATAL_PAGE_FAULT:
        return "fatal page fault";
    case ZX_EXCP_UNDEFINED_INSTRUCTION:
        return "undefined instruction";
    case ZX_EXCP_GENERAL:
        return "general fault";
    case ZX_EXCP_SW_BREAKPOINT:
        return "software breakpoint";
    case ZX_EXCP_HW_BREAKPOINT:
        return "hardware breakpoint";
    case ZX_EXCP_UNALIGNED_ACCESS:
        return "alignment fault";
    case ZX_EXCP_POLICY_ERROR:
        return "policy error";
    case ZX_EXCP_PROCESS_STARTING:
        return "process starting";
    case ZX_EXCP_THREAD_STARTING:
        return "thread starting";
    case ZX_EXCP_THREAD_EXITING:
        return "thread exiting";
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
// - thread channel
// - process
// - job (first owning job, then its parent job, and so on up to root job)
class ExceptionPortIterator final {
public:
    // All exception handler types, including both ports and channels.
    // TODO(ZX-3072): remove ports once everyone is switched to channels.
    enum class Type {
        NONE,
        JOB_DEBUGGER,
        DEBUGGER,
        THREAD,
        THREAD_CHANNEL,
        PROCESS,
        JOB
    };

    explicit ExceptionPortIterator(ThreadDispatcher* thread,
                                   fbl::RefPtr<ExceptionDispatcher> exception)
        : thread_(thread), exception_(ktl::move(exception)), previous_type_(Type::NONE) {}

    // Returns true with |eport| filled in if the caller should dispatch the
    // exception to the given exception port.
    // Returns true with empty |eport| and filled |channel_result| if the
    // exception was sent to a channel handler.
    // Returns false if there are no more to try.
    bool Next(fbl::RefPtr<ExceptionPort>* eport, zx_status_t* channel_result) {
        eport->reset(nullptr);
        bool sent_to_channel = false;

        while (true) {
            switch (previous_type_) {
                case Type::NONE:
                    *eport = thread_->process()->debugger_exception_port();
                    previous_type_ = Type::DEBUGGER;
                    break;
                case Type::DEBUGGER:
                    *eport = thread_->exception_port();
                    previous_type_ = Type::THREAD;
                    break;
                case Type::THREAD:
                    *channel_result = thread_->HandleException(exception_, &sent_to_channel);
                    previous_type_ = Type::THREAD_CHANNEL;
                    break;
                case Type::THREAD_CHANNEL:
                    *eport = thread_->process()->exception_port();
                    previous_type_ = Type::PROCESS;
                    break;
                case Type::PROCESS:
                    previous_job_ = thread_->process()->job();
                    *eport = previous_job_->exception_port();
                    previous_type_ = Type::JOB;
                    break;
                case Type::JOB:
                    previous_job_ = previous_job_->parent();
                    if (previous_job_) {
                        *eport = previous_job_->exception_port();
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

            // Only service one port or channel exception per call, not both.
            DEBUG_ASSERT(!(eport->get() && sent_to_channel));

            // Return to the caller once we find either a port to process or
            // a channel that was processed.
            if (eport->get() || sent_to_channel) {
                return true;
            }

        }
        __UNREACHABLE;
    }

private:
    ThreadDispatcher* thread_;
    fbl::RefPtr<ExceptionDispatcher> exception_;
    Type previous_type_;
    // Jobs are traversed up their hierarchy. This is the previous one.
    fbl::RefPtr<JobDispatcher> previous_job_;

    DISALLOW_COPY_ASSIGN_AND_MOVE(ExceptionPortIterator);
};

static zx_status_t try_exception_handler(fbl::RefPtr<ExceptionPort> eport,
                                         ThreadDispatcher* thread,
                                         const zx_exception_report_t* report,
                                         const arch_exception_context_t* arch_context,
                                         ThreadState::Exception* estatus) {
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
                                                 const arch_exception_context_t* context,
                                                 ThreadDispatcher* thread,
                                                 bool* out_processed) {
    *out_processed = false;

    zx_exception_report_t report;
    ExceptionPort::BuildArchReport(&report, exception_type, context);

    fbl::RefPtr<ExceptionDispatcher> exception = ExceptionDispatcher::Create(
        fbl::WrapRefPtr(thread), exception_type, &report, context);
    if (!exception) {
        // No memory to create the exception, we just have to drop it which
        // will kill the process.
        printf("KERN: failed to allocate memory for %s exception in user thread %lu.%lu\n",
               excp_type_to_string(exception_type), thread->process()->get_koid(),
               thread->get_koid());
        return HS_NOT_HANDLED;
    }

    // Most of the time we'll be holding the last reference to the exception
    // when this function exits, but if the task is killed we return HS_KILLED
    // without waiting for the handler which means someone may still have a
    // handle to the exception.
    //
    // For simplicity and to catch any unhandled status cases below, just clean
    // out the exception before returning no matter what.
    auto exception_cleaner = fbl::MakeAutoCall([&exception]() { exception->Clear(); });

    ExceptionPortIterator iter(thread, exception);
    fbl::RefPtr<ExceptionPort> eport;
    // This should always be overwritten, either by iter.Next() for channels
    // or try_exception_handler() for ports.
    zx_status_t status = ZX_ERR_INTERNAL;

    while (iter.Next(&eport, &status)) {
        // Initialize for paranoia's sake.
        ThreadState::Exception estatus = ThreadState::Exception::UNPROCESSED;
        if (eport) {
            status = try_exception_handler(eport, thread, &report, context, &estatus);
        } else {
            // Channels return a single status value, for now map this to the
            // existing port logic which combines resume/try_next into ZX_OK.
            if (status == ZX_OK) {
                estatus = ThreadState::Exception::RESUME;
            } else if (status == ZX_ERR_NEXT) {
                status = ZX_OK;
                estatus = ThreadState::Exception::TRY_NEXT;
            }
        }

        LTRACEF("handler returned %d/%d\n",
                static_cast<int>(status), static_cast<int>(estatus));
        switch (status) {
        case ZX_ERR_INTERNAL_INTR_KILLED:
            // thread was killed, probably with zx_task_kill
            return HS_KILLED;
        case ZX_OK:
            switch (estatus) {
            case ThreadState::Exception::TRY_NEXT:
                *out_processed = true;
                break;
            case ThreadState::Exception::RESUME:
                return HS_RESUME;
            default:
                ASSERT_MSG(0, "invalid exception status %d",
                           static_cast<int>(estatus));
                __UNREACHABLE;
            }
            break;
        default:
            // Instead of requiring exception processing to only return
            // specific kinds of errors (and thus requiring us to be updated
            // every time a change causes a new error to be returned), treat
            // all other errors as fatal. It's debatable whether to give the
            // next handler a try or immediately kill the task. By immediately
            // killing the task we bypass the root job exception handler,
            // but it feels safer.
            // TODO(ZX-2853): Are there times when we should try harder to
            // process the exception?
            // Print something to give the user a clue.
            printf("KERN: Error %d processing exception in user thread %lu.%lu\n",
                   status, thread->process()->get_koid(), thread->get_koid());
            // Still mark the exception as processed so that we don't trigger
            // later bare-bones crash reporting (TRACE_EXCEPTIONS).
            *out_processed = true;
            return HS_NOT_HANDLED;
        }
    }

    return HS_NOT_HANDLED;
}

// Dispatches an exception to the appropriate handler. Called by arch code
// when it cannot handle an exception.
//
// If we return ZX_OK, the caller is expected to resume the thread "as if"
// nothing happened, the handler is expected to have modified state such that
// resumption is possible.
//
// If we return ZX_ERR_BAD_STATE, the current thread is not a user thread
// (i.e., not associated with a ThreadDispatcher).
//
// Otherwise, we cause the current thread to exit and do not return at all.
//
// TODO(dje): Support unwinding from this exception and introducing a different
// exception?
zx_status_t dispatch_user_exception(uint exception_type,
                                    const arch_exception_context_t* context) {
    LTRACEF("type %u, context %p\n", exception_type, context);

    thread_t* lk_thread = get_current_thread();
    ThreadDispatcher* thread = lk_thread->user_thread;
    if (unlikely(!thread)) {
        // The current thread is not a user thread; bail.
        return ZX_ERR_BAD_STATE;
    }

    // From now until the exception is resolved the thread is in an exception.
    ThreadDispatcher::AutoBlocked by(ThreadDispatcher::Blocked::EXCEPTION);

    arch_install_context_regs(lk_thread, context);
    bool processed;
    handler_status_t hstatus =
        exception_handler_worker(exception_type, context, thread, &processed);
    arch_remove_context_regs(lk_thread);

    switch (hstatus) {
        case HS_RESUME:
            return ZX_OK;
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
        char pname[ZX_MAX_NAME_LEN];
        process->get_name(pname);
        char tname[ZX_MAX_NAME_LEN];
        thread->get_name(tname);
        printf("KERN: %s in user thread '%s' in process '%s'\n",
               excp_type_to_string(exception_type), tname, pname);

        arch_dump_exception_context(context);
    }
#endif

    // kill our process
    process->Kill(ZX_TASK_RETCODE_EXCEPTION_KILL);

    // exit
    thread->Exit();

    // should not get here
    panic("fell out of thread exit somehow!\n");
    __UNREACHABLE;
}

zx_status_t dispatch_debug_exception(fbl::RefPtr<ExceptionPort> eport,
                                     uint exception_type,
                                     const arch_exception_context_t* context) {
    LTRACEF("type %u, context %p\n", exception_type, context);

    thread_t* lk_thread = get_current_thread();
    ThreadDispatcher* thread = lk_thread->user_thread;
    // This function can only be called on behalf of user threads.
    DEBUG_ASSERT(thread);

    // From now until the exception is resolved the thread is in an exception.
    ThreadDispatcher::AutoBlocked by(ThreadDispatcher::Blocked::EXCEPTION);

    arch_install_context_regs(lk_thread, context);
    auto ac = fbl::MakeAutoCall([&lk_thread]() {
        arch_remove_context_regs(lk_thread);
    });

    zx_exception_report_t report;
    ExceptionPort::BuildArchReport(&report, exception_type, context);

    ThreadState::Exception estatus;
    return thread->ExceptionHandlerExchange(eport, &report, context, &estatus);
    // We can ignore |estatus| here (TRY_NEXT/RESUME) as they're not used.
}
