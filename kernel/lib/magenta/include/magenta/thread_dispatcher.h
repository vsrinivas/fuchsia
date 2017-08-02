// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <sys/types.h>

#include <kernel/event.h>
#include <kernel/mutex.h>
#include <kernel/thread.h>
#include <kernel/vm/vm_address_region.h>

#include <lib/dpc.h>

#include <magenta/channel_dispatcher.h>
#include <magenta/dispatcher.h>
#include <magenta/exception.h>
#include <magenta/excp_port.h>
#include <magenta/futex_node.h>
#include <magenta/state_tracker.h>
#include <magenta/syscalls/exception.h>

#include <mxtl/canary.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/string_piece.h>

class ProcessDispatcher;

class ThreadDispatcher : public Dispatcher {
public:
    // Traits to belong in the parent process's list.
    struct ThreadListTraits {
        static mxtl::DoublyLinkedListNodeState<ThreadDispatcher*>& node_state(
            ThreadDispatcher& obj) {
            return obj.dll_thread_;
        }
    };

    // state of the thread
    enum class State {
        INITIAL,     // newly created thread
        INITIALIZED, // LK thread state is initialized
        RUNNING,     // thread is running
        SUSPENDED,   // thread is suspended
        DYING,       // thread has been signaled for kill, but has not exited yet
        DEAD,        // thread has exited and is not running
    };

    // the exception status (disposition?) of the thread
    enum class ExceptionStatus {
        // The thread is not in an exception
        IDLE,

        // The thread is blocked in an exception, waiting for a response
        UNPROCESSED,

        // The exception is unhandled, try the next handler.
        // If this is the last handler then the process is killed.
        // As an analogy, this would be like typing "c" in gdb after a
        // segfault. In linux the signal would be delivered to the thread,
        // which would either terminate the process or run a signal handler if
        // defined. In magenta this gives the next signal handler in the list
        // a crack at // the exception.
        TRY_NEXT,

        // The exception has been handled, resume the thread.
        // As an analogy, this would be like typing "sig 0" in gdb after a
        // segfault. The faulting instruction will be retried. If, for example,
        // it segfaults again then the user is back in the debugger again,
        // which is working as intended.
        // Note: We don't, currently at least, support delivering a different
        // exception (signal in linux parlance) to the thread. As an analogy,
        // this would be like typing "sig 8" in gdb after getting a segfault
        // (which is signal 11).
        RESUME,
    };

    static status_t Create(mxtl::RefPtr<ProcessDispatcher> process, uint32_t flags,
                           mxtl::StringPiece name,
                           mxtl::RefPtr<Dispatcher>* out_dispatcher,
                           mx_rights_t* out_rights);
    ~ThreadDispatcher();

    static ThreadDispatcher* GetCurrent() {
        return reinterpret_cast<ThreadDispatcher*>(get_current_thread()->user_thread);
    }

    // Dispatcher implementation.
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_THREAD; }
    StateTracker* get_state_tracker() final { return &state_tracker_; }
    void on_zero_handles() final;
    mx_koid_t get_related_koid() const final;

    // Performs initialization on a newly constructed ThreadDispatcher
    // If this fails, then the object is invalid and should be deleted
    status_t Initialize(const char* name, size_t len);
    status_t Start(uintptr_t pc, uintptr_t sp, uintptr_t arg1, uintptr_t arg2,
                   bool initial_thread);
    void Exit() __NO_RETURN;
    void Kill();
    void DispatcherClosed();

    status_t Suspend();
    status_t Resume();

    // accessors
    ProcessDispatcher* process() { return process_.get(); }

    FutexNode* futex_node() { return &futex_node_; }
    status_t set_name(const char* name, size_t len) final;
    void get_name(char out_name[MX_MAX_NAME_LEN]) const final;
    uint64_t runtime_ns() const { return thread_runtime(&thread_); }

    status_t SetExceptionPort(mxtl::RefPtr<ExceptionPort> eport);
    // Returns true if a port had been set.
    bool ResetExceptionPort(bool quietly);
    mxtl::RefPtr<ExceptionPort> exception_port();

    // Send a report to the associated exception handler of |eport| and wait
    // for a response.
    // Note this takes a specific exception port as an argument because there are several:
    // debugger, thread, process, and system. The kind of the exception port is
    // specified by |eport->type()|.
    // Returns:
    // MX_OK: the exception was handled in some way, and |*out_estatus|
    // specifies how.
    // MX_ERR_INTERNAL_INTR_KILLED: the thread was killed (probably via mx_task_kill)
    status_t ExceptionHandlerExchange(mxtl::RefPtr<ExceptionPort> eport,
                                      const mx_exception_report_t* report,
                                      const arch_exception_context_t* arch_context,
                                      ExceptionStatus* out_estatus);
    // Called when an exception handler is finished processing the exception.
    status_t MarkExceptionHandled(ExceptionStatus estatus);
    // Called when exception port |eport| is removed.
    // If the thread is waiting for the associated exception handler, continue
    // exception processing as if the exception port had not been installed.
    void OnExceptionPortRemoval(const mxtl::RefPtr<ExceptionPort>& eport);
    // Return true if waiting for an exception response.
    // |state_lock_| must be held.
    bool InExceptionLocked() TA_REQ(state_lock_);
    // Assuming the thread is stopped waiting for an exception response,
    // fill in |*report| with the exception report.
    // Returns MX_ERR_BAD_STATE if not in an exception.
    status_t GetExceptionReport(mx_exception_report_t* report);

    // Fetch the state of the thread for userspace tools.
    mx_status_t GetInfoForUserspace(mx_info_thread_t* info);

    // Fetch per thread stats for userspace.
    mx_status_t GetStatsForUserspace(mx_info_thread_stats_t* info);

    // For debugger usage.
    // TODO(dje): The term "state" here conflicts with "state tracker".
    uint32_t get_num_state_kinds() const;
    // TODO(dje): Consider passing an Array<uint8_t> here and in WriteState.
    status_t ReadState(uint32_t state_kind, void* buffer, uint32_t* buffer_len);
    // |priv| = true -> allow setting privileged values, otherwise leave them unchanged
    // This is useful for, for example, flags registers that consist of both
    // privileged and unprivileged fields.
    status_t WriteState(uint32_t state_kind, const void* buffer, uint32_t buffer_len, bool priv);

    // For ChannelDispatcher use.
    ChannelDispatcher::MessageWaiter* GetMessageWaiter() { return &channel_waiter_; }

private:
    ThreadDispatcher(mxtl::RefPtr<ProcessDispatcher> process, uint32_t flags);
    ThreadDispatcher(const ThreadDispatcher&) = delete;
    ThreadDispatcher& operator=(const ThreadDispatcher&) = delete;

    // kernel level entry point
    static int StartRoutine(void* arg);

    // callback from kernel when thread is exiting, just before it stops for good.
    void Exiting();

    // callback from kernel when thread is suspending
    void Suspending();
    // callback from kernel when thread is resuming
    void Resuming();

    // Dispatch routine for state changes that LK tells us about
    static void ThreadUserCallback(enum thread_user_state_change new_state, void* arg);

    // change states of the object, do what is appropriate for the state transition
    void SetStateLocked(State) TA_REQ(state_lock_);

    mxtl::Canary<mxtl::magic("THRD")> canary_;

    // The containing process holds a list of all its threads.
    mxtl::DoublyLinkedListNodeState<ThreadDispatcher*> dll_thread_;

    // a ref pointer back to the parent process
    mxtl::RefPtr<ProcessDispatcher> process_;

    // User thread starting register values.
    uintptr_t user_entry_ = 0;
    uintptr_t user_sp_ = 0;
    uintptr_t user_arg1_ = 0;
    uintptr_t user_arg2_ = 0;

    // our State
    State state_ TA_GUARDED(state_lock_) = State::INITIAL;
    Mutex state_lock_;

    // Node for linked list of threads blocked on a futex
    FutexNode futex_node_;

    StateTracker state_tracker_;

    // A thread-level exception port for this thread.
    mxtl::RefPtr<ExceptionPort> exception_port_ TA_GUARDED(exception_lock_);
    Mutex exception_lock_;

    // Support for sending an exception to an exception handler and then waiting for a response.
    ExceptionStatus exception_status_ TA_GUARDED(state_lock_)
        = ExceptionStatus::IDLE;
    // The exception port of the handler the thread is waiting for a response from.
    mxtl::RefPtr<ExceptionPort> exception_wait_port_ TA_GUARDED(state_lock_);
    const mx_exception_report_t* exception_report_ TA_GUARDED(state_lock_);
    event_t exception_event_ =
        EVENT_INITIAL_VALUE(exception_event_, false, EVENT_FLAG_AUTOUNSIGNAL);

    // cleanup dpc structure
    dpc_t cleanup_dpc_ = {LIST_INITIAL_CLEARED_VALUE, nullptr, nullptr};

    // Used to protect thread name read/writes
    mutable SpinLock name_lock_;

    // hold a reference to the mapping and vmar used to wrap the mapping of this
    // thread's kernel stack
    mxtl::RefPtr<VmMapping> kstack_mapping_;
    mxtl::RefPtr<VmAddressRegion> kstack_vmar_;
#if __has_feature(safe_stack)
    mxtl::RefPtr<VmMapping> unsafe_kstack_mapping_;
    mxtl::RefPtr<VmAddressRegion> unsafe_kstack_vmar_;
#endif

    // Per-thread structure used while waiting in a ChannelDispatcher::Call.
    // Needed to support the requirements of being able to interrupt a Call
    // in order to suspend a thread.
    ChannelDispatcher::MessageWaiter channel_waiter_;

    // LK thread structure
    // put last to ease debugging since this is a pretty large structure
    // (~1.5K on x86_64).
    // Also, a simple experiment to move this to the first member (after the
    // canary) resulted in a 1K increase in text size (x86_64).
    thread_t thread_ = {};
};

const char* StateToString(ThreadDispatcher::State state);
