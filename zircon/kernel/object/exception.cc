// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch.h>
#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <stdio.h>
#include <trace.h>
#include <zircon/syscalls/object.h>

#include <arch/exception.h>
#include <fbl/auto_call.h>
#include <object/exception_dispatcher.h>
#include <object/job_dispatcher.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>

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
// Exception handlers are tried in the following order:
// - debugger
// - thread
// - process
// - debugger (in dealing with a second-chance exception)
// - job (first owning job, then its parent job, and so on up to root job)
class ExceptionHandlerIterator final {
 public:
  explicit ExceptionHandlerIterator(ThreadDispatcher* thread,
                                    fbl::RefPtr<ExceptionDispatcher> exception)
      : thread_(thread), exception_(ktl::move(exception)) {}

  // Sends the exception to the next registered handler, starting with
  // ZX_EXCEPTION_CHANNEL_TYPE_DEBUGGER (the process debug channel) on the first
  // call.
  //
  // Returns true with and fills |result| if the exception was sent to a
  // handler, or returns false if there are no more to try. Do not call this
  // function again after it returns false.
  bool Next(zx_status_t* result) {
    bool sent = false;

    while (true) {
      // This state may change during the handling of the debugger exception
      // channel. Accordingly, we check its value before the next round of
      // handling to be sure of the proper sequencing.
      bool second_chance = exception_->IsSecondChance();

      switch (next_type_) {
        case ZX_EXCEPTION_CHANNEL_TYPE_DEBUGGER:
          *result = thread_->HandleException(
              thread_->process()->exceptionate(Exceptionate::Type::kDebug), exception_, &sent);
          if (second_chance) {
            next_type_ = ZX_EXCEPTION_CHANNEL_TYPE_JOB;
            next_job_ = thread_->process()->job();
          } else {
            next_type_ = ZX_EXCEPTION_CHANNEL_TYPE_THREAD;
          }
          break;
        case ZX_EXCEPTION_CHANNEL_TYPE_THREAD:
          *result = thread_->HandleException(thread_->exceptionate(), exception_, &sent);
          next_type_ = ZX_EXCEPTION_CHANNEL_TYPE_PROCESS;
          break;
        case ZX_EXCEPTION_CHANNEL_TYPE_PROCESS:
          *result = thread_->HandleException(
              thread_->process()->exceptionate(Exceptionate::Type::kStandard), exception_, &sent);

          if (second_chance) {
            next_type_ = ZX_EXCEPTION_CHANNEL_TYPE_DEBUGGER;
          } else {
            next_type_ = ZX_EXCEPTION_CHANNEL_TYPE_JOB;
            next_job_ = thread_->process()->job();
          }
          break;
        case ZX_EXCEPTION_CHANNEL_TYPE_JOB:
          if (next_job_ == nullptr) {
            // Reached the root job and there was no handler.
            return false;
          }
          *result = thread_->HandleException(next_job_->exceptionate(Exceptionate::Type::kStandard),
                                             exception_, &sent);
          next_job_ = next_job_->parent();
          break;
        default:
          ASSERT_MSG(0, "unexpected exception type %u", next_type_);
          __UNREACHABLE;
      }

      // Return to the caller once a handler was activated.
      if (sent) {
        return true;
      }
    }
    __UNREACHABLE;
  }

 private:
  ThreadDispatcher* thread_;
  fbl::RefPtr<ExceptionDispatcher> exception_;
  uint32_t next_type_ = ZX_EXCEPTION_CHANNEL_TYPE_DEBUGGER;
  fbl::RefPtr<JobDispatcher> next_job_;

  DISALLOW_COPY_ASSIGN_AND_MOVE(ExceptionHandlerIterator);
};

// Subroutine of dispatch_user_exception to simplify the code.
// One useful thing this does is guarantee ExceptionHandlerIterator is properly
// destructed.
//
// |*out_processed| is set to a boolean indicating if at least one
// handler processed the exception.
//
// Returns:
//   ZX_OK if the thread has been resumed.
//   ZX_ERR_NEXT if we ran out of handlers before the thread resumed.
//   ZX_ERR_INTERNAL_INTR_KILLED if the thread was killed.
//   ZX_ERR_NO_MEMORY on allocation failure (TODO(fxbug.dev/33566): remove this case)
static zx_status_t exception_handler_worker(uint exception_type,
                                            const arch_exception_context_t* context,
                                            ThreadDispatcher* thread, bool* out_processed) {
  *out_processed = false;

  zx_exception_report_t report = ExceptionDispatcher::BuildArchReport(exception_type, *context);

  fbl::RefPtr<ExceptionDispatcher> exception =
      ExceptionDispatcher::Create(fbl::RefPtr(thread), exception_type, &report, context);
  if (!exception) {
    // No memory to create the exception, we just have to drop it which
    // will kill the process.
    printf("KERN: failed to allocate memory for %s exception in user thread %lu.%lu\n",
           excp_type_to_string(exception_type), thread->process()->get_koid(), thread->get_koid());
    return ZX_ERR_NO_MEMORY;
  }

  // Most of the time we'll be holding the last reference to the exception
  // when this function exits, but if the task is killed we return HS_KILLED
  // without waiting for the handler which means someone may still have a
  // handle to the exception.
  //
  // For simplicity and to catch any unhandled status cases below, just clean
  // out the exception before returning no matter what.
  auto exception_cleaner = fbl::MakeAutoCall([&exception]() { exception->Clear(); });

  ExceptionHandlerIterator iter(thread, exception);
  zx_status_t status = ZX_ERR_NEXT;
  while (iter.Next(&status)) {
    LTRACEF("handler returned %d\n", status);

    // ZX_ERR_NEXT means the handler wants to pass it up to the next in the
    // chain, keep looping but mark that at least one handler saw the exception.
    if (status == ZX_ERR_NEXT) {
      *out_processed = true;
      continue;
    }

    // Anything other than ZX_ERR_NEXT means we're done.
    return status;
  }

  // If we got here we ran out of handlers and nobody resumed the thread.
  return ZX_ERR_NEXT;
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
                                    const arch_exception_context_t* arch_context) {
  LTRACEF("type %u, context %p\n", exception_type, arch_context);

  ThreadDispatcher* thread = ThreadDispatcher::GetCurrent();
  if (unlikely(!thread)) {
    // The current thread is not a user thread; bail.
    return ZX_ERR_BAD_STATE;
  }

  // From now until the exception is resolved the thread is in an exception.
  ThreadDispatcher::AutoBlocked by(ThreadDispatcher::Blocked::EXCEPTION);

  bool processed = false;
  zx_status_t status;
  {
    // We're about to handle the exception.  Use a |ScopedThreadExceptionContext| to make the
    // thread's user register state available to debuggers and exception handlers while the thread
    // is "in exception".
    ScopedThreadExceptionContext context(arch_context);
    status = exception_handler_worker(exception_type, arch_context, thread, &processed);
  }

  if (status == ZX_OK) {
    return ZX_OK;
  }

  // If the thread wasn't resumed or explicitly killed, kill the whole process.
  if (status != ZX_ERR_INTERNAL_INTR_KILLED) {
    auto process = thread->process();

#if TRACE_EXCEPTIONS
    // If no handlers even saw the exception, dump some info. Normally at least
    // crashsvc will handle the exception and make a smarter decision about what
    // to do with it, but in case it doesn't, dump some info to the kernel logs.
    if (!processed) {
      char pname[ZX_MAX_NAME_LEN];
      process->get_name(pname);
      char tname[ZX_MAX_NAME_LEN];
      thread->get_name(tname);
      printf("KERN: %s in user thread '%s' in process '%s'\n", excp_type_to_string(exception_type),
             tname, pname);

      arch_dump_exception_context(arch_context);
    }
#endif

    printf("KERN: terminating process\n");
    process->Kill(ZX_TASK_RETCODE_EXCEPTION_KILL);
  }

  // Either the current thread or its whole process was killed, we can now stop
  // it from running.
  ThreadDispatcher::ExitCurrent();
  panic("fell out of thread exit somehow!\n");
  __UNREACHABLE;
}
