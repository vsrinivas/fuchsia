// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <string.h>

#include <magenta/exception.h>
#include <magenta/excp_port.h>
#include <magenta/magenta.h>
#include <magenta/port_dispatcher.h>
#include <magenta/process_dispatcher.h>
#include <magenta/thread_dispatcher.h>
#include <magenta/user_thread.h>

#include <mxalloc/new.h>

#include <trace.h>

#define LOCAL_TRACE 0

static IOP_Packet* MakePacket(uint64_t key, const mx_exception_report_t* report, size_t size) {
    auto pk = IOP_Packet::Alloc(size + sizeof(mx_packet_header_t));
    if (!pk)
        return nullptr;

    auto pkt_data = reinterpret_cast<mx_exception_packet_t*>(
        reinterpret_cast<char*>(pk) + sizeof(IOP_Packet));

    memcpy(&pkt_data->report, report, size);
    pkt_data->hdr.key = key;
    pkt_data->hdr.type = MX_PORT_PKT_TYPE_EXCEPTION;
    pkt_data->hdr.extra = 0; // currently unused

    return pk;
}

// static
mx_status_t ExceptionPort::Create(Type type, mxtl::RefPtr<PortDispatcher> port, uint64_t port_key,
                                  mxtl::RefPtr<ExceptionPort>* out_eport) {
    AllocChecker ac;
    auto eport = new (&ac) ExceptionPort(type, mxtl::move(port), port_key);
    if (!ac.check())
        return ERR_NO_MEMORY;

    // ExceptionPort's ctor causes the first ref to be adopted,
    // so we should only wrap.
    *out_eport = mxtl::WrapRefPtr<ExceptionPort>(eport);
    return NO_ERROR;
}

ExceptionPort::ExceptionPort(Type type, mxtl::RefPtr<PortDispatcher> port, uint64_t port_key)
    : type_(type), port_key_(port_key), port_(port) {
    LTRACE_ENTRY_OBJ;
    DEBUG_ASSERT(port_ != nullptr);
    port_->LinkExceptionPort(this);
}

ExceptionPort::~ExceptionPort() {
    LTRACE_ENTRY_OBJ;
    DEBUG_ASSERT(port_ == nullptr);
    DEBUG_ASSERT(!InContainer());
    DEBUG_ASSERT(!IsBoundLocked());
}

void ExceptionPort::SetSystemTarget() {
    canary_.Assert();

    LTRACE_ENTRY_OBJ;
    AutoLock lock(&lock_);
    DEBUG_ASSERT_MSG(type_ == Type::SYSTEM,
                     "unexpected type %d", static_cast<int>(type_));
    DEBUG_ASSERT(!IsBoundLocked());
    DEBUG_ASSERT(port_ != nullptr);
    bound_to_system_ = true;
}

void ExceptionPort::SetTarget(const mxtl::RefPtr<ProcessDispatcher>& target) {
    canary_.Assert();

    LTRACE_ENTRY_OBJ;
    AutoLock lock(&lock_);
    DEBUG_ASSERT_MSG(type_ == Type::DEBUGGER || type_ == Type::PROCESS,
                     "unexpected type %d", static_cast<int>(type_));
    DEBUG_ASSERT(!IsBoundLocked());
    DEBUG_ASSERT(target != nullptr);
    DEBUG_ASSERT(port_ != nullptr);
    target_ = target;
}

void ExceptionPort::SetTarget(const mxtl::RefPtr<ThreadDispatcher>& target) {
    canary_.Assert();

    LTRACE_ENTRY_OBJ;
    AutoLock lock(&lock_);
    DEBUG_ASSERT_MSG(type_ == Type::THREAD,
                     "unexpected type %d", static_cast<int>(type_));
    DEBUG_ASSERT(!IsBoundLocked());
    DEBUG_ASSERT(target != nullptr);
    DEBUG_ASSERT(port_ != nullptr);
    target_ = target;
}

// Called by PortDispatcher after unlinking us from its eport list.
void ExceptionPort::OnPortZeroHandles() {
    canary_.Assert();

    // TODO(dje): Add a way to mark specific ports as unbinding quietly
    // when auto-unbinding.
    static const bool default_quietness = false;

    LTRACE_ENTRY_OBJ;
    AutoLock lock(&lock_);
    if (port_ == nullptr) {
        // Already unbound. This can happen when
        // PortDispatcher::on_zero_handles and a manual unbind (via
        // mx_task_bind_exception_port) race with each other.
        LTRACEF("already unbound\n");
        DEBUG_ASSERT(!IsBoundLocked());
        return;
    }

    // Unbind ourselves from our target if necessary. At the end of this
    // block, some thread (ours or another) will have called back into our
    // ::OnTargetUnbind method, cleaning up our target/port references. The
    // "other thread" case can happen if another thread manually unbinds after
    // we release the lock.
    if (!IsBoundLocked()) {
        // Created but never bound.
        lock.release();
        // Simulate an unbinding to finish cleaning up.
        OnTargetUnbind();
    } else if (type_ == Type::SYSTEM) {
        DEBUG_ASSERT(bound_to_system_);
        DEBUG_ASSERT(target_ == nullptr);
        lock.release();  // The target may call our ::OnTargetUnbind
        ResetSystemExceptionPort();
    } else if (type_ == Type::PROCESS || type_ == Type::DEBUGGER) {
        DEBUG_ASSERT(!bound_to_system_);
        DEBUG_ASSERT(target_ != nullptr);
        auto process = DownCastDispatcher<ProcessDispatcher>(&target_);
        DEBUG_ASSERT(process != nullptr);
        lock.release();  // The target may call our ::OnTargetUnbind
        process->ResetExceptionPort(type_ == Type::DEBUGGER, default_quietness);
    } else if (type_ == Type::THREAD) {
        DEBUG_ASSERT(!bound_to_system_);
        DEBUG_ASSERT(target_ != nullptr);
        auto thread = DownCastDispatcher<ThreadDispatcher>(&target_);
        DEBUG_ASSERT(thread != nullptr);
        lock.release();  // The target may call our ::OnTargetUnbind
        thread->ResetExceptionPort(default_quietness);
    } else {
        PANIC("unexpected type %d", static_cast<int>(type_));
    }
    // All cases must release the lock.
    DEBUG_ASSERT(!lock_.IsHeld());

#if (LK_DEBUGLEVEL > 1)
    // The target should have called back into ::OnTargetUnbind by this point,
    // cleaning up our references.
    {
        AutoLock lock2(&lock_);
        DEBUG_ASSERT(port_ == nullptr);
        DEBUG_ASSERT(!IsBoundLocked());
    }
#endif  // if (LK_DEBUGLEVEL > 1)

    LTRACE_EXIT_OBJ;
}

void ExceptionPort::OnTargetUnbind() {
    canary_.Assert();

    LTRACE_ENTRY_OBJ;
    mxtl::RefPtr<PortDispatcher> port;
    {
        AutoLock lock(&lock_);
        if (port_ == nullptr) {
            // Already unbound.
            // This could happen if ::OnPortZeroHandles releases the
            // lock and another thread immediately does a manual unbind
            // via mx_task_bind_exception_port.
            DEBUG_ASSERT(!IsBoundLocked());
            return;
        }
        // Clear port_, indicating that this ExceptionPort has been unbound.
        port_.swap(port);

        // Drop references to the target.
        // We may not have a target if the binding (Set*Target) never happened,
        // so don't require that we're bound.
        bound_to_system_ = false;
        target_.reset();
    }
    // It should actually be safe to hold our lock while calling into
    // the PortDispatcher, but there's no reason to.

    // Unlink ourselves from the PortDispatcher's list.
    // No-op if this method was ultimately called from
    // PortDispatcher:on_zero_handles (via ::OnPortZeroHandles).
    port->UnlinkExceptionPort(this);

    LTRACE_EXIT_OBJ;
}

mx_status_t ExceptionPort::SendReport(const mx_exception_report_t* report) {
    canary_.Assert();

    AutoLock lock(&lock_);
    LTRACEF("%s, type %u, pid %" PRIu64 ", tid %" PRIu64 "\n",
            port_ == nullptr ? "Not sending exception report on unbound port"
                : "Sending exception report",
            report->header.type, report->context.pid, report->context.tid);
    if (port_ == nullptr) {
        // The port has been unbound.
        return ERR_PEER_CLOSED;
    }

    auto iopk = MakePacket(port_key_, report, sizeof(*report));
    if (!iopk)
        return ERR_NO_MEMORY;

    return port_->Queue(iopk);
}

void ExceptionPort::BuildReport(mx_exception_report_t* report, uint32_t type,
                                mx_koid_t pid, mx_koid_t tid) {
    memset(report, 0, sizeof(*report));
    report->header.size = sizeof(*report);
    report->header.type = type;
    report->context.pid = pid;
    report->context.tid = tid;
}

void ExceptionPort::BuildSuspendResumeReport(mx_exception_report_t* report,
                                             uint32_t type,
                                             UserThread* thread) {
    mx_koid_t pid = thread->process()->get_koid();
    mx_koid_t tid = thread->get_koid();
    BuildReport(report, type, pid, tid);
    // TODO(dje): IWBN to fill in pc
    arch_fill_in_suspension_context(report);
}

void ExceptionPort::OnThreadStart(UserThread* thread) {
    canary_.Assert();

    mx_koid_t pid = thread->process()->get_koid();
    mx_koid_t tid = thread->get_koid();
    LTRACEF("thread %" PRIu64 ".%" PRIu64 " started\n", pid, tid);
    mx_exception_report_t report;
    BuildReport(&report, MX_EXCP_THREAD_STARTING, pid, tid);
    arch_exception_context_t context;
    // There is no iframe at the moment. We'll need one (or equivalent) if/when
    // we want to make $pc, $sp available.
    memset(&context, 0, sizeof(context));
    UserThread::ExceptionStatus estatus;
    auto status = thread->ExceptionHandlerExchange(mxtl::RefPtr<ExceptionPort>(this), &report, &context, &estatus);
    if (status != NO_ERROR) {
        // Ignore any errors. There's nothing we can do here, and
        // we still want the thread to run. It's possible the thread was
        // killed (status == ERR_INTERRUPTED), the kernel will kill the
        // thread shortly.
    }
}

void ExceptionPort::OnThreadSuspending(UserThread* thread) {
    canary_.Assert();

    mx_koid_t pid = thread->process()->get_koid();
    mx_koid_t tid = thread->get_koid();
    LTRACEF("thread %" PRIu64 ".%" PRIu64 " suspending\n", pid, tid);

    // A note on the tense of the words used here: suspending vs suspended.
    // "suspending" is used in the internal context because we're still
    // in the process of suspending the thread. "suspended" is used in the
    // external context because once the debugger receives the "suspended"
    // report it can assume the thread is, for its purposes, suspended.

    mx_exception_report_t report;
    BuildSuspendResumeReport(&report, MX_EXCP_THREAD_SUSPENDED, thread);
    // The result is ignored, not much else we can do.
    SendReport(&report);
}

void ExceptionPort::OnThreadResuming(UserThread* thread) {
    canary_.Assert();

    mx_koid_t pid = thread->process()->get_koid();
    mx_koid_t tid = thread->get_koid();
    LTRACEF("thread %" PRIu64 ".%" PRIu64 " resuming\n", pid, tid);

    // See OnThreadSuspending for a note on the tense of the words uses here:
    // suspending vs suspended.

    mx_exception_report_t report;
    BuildSuspendResumeReport(&report, MX_EXCP_THREAD_RESUMED, thread);
    // The result is ignored, not much else we can do.
    SendReport(&report);
}

// This isn't called for every process's destruction, only for processes that
// have a bound process or debugger exception export.

void ExceptionPort::OnProcessExit(ProcessDispatcher* process) {
    canary_.Assert();

    mx_koid_t pid = process->get_koid();
    LTRACEF("process %" PRIu64 " gone\n", pid);
    mx_exception_report_t report;
    BuildReport(&report, MX_EXCP_GONE, pid, MX_KOID_INVALID);
    // The result is ignored, not much else we can do.
    SendReport(&report);
}

// This isn't called for every thread's destruction, only for threads that
// have a thread-specific exception handler.

void ExceptionPort::OnThreadExit(UserThread* thread) {
    canary_.Assert();

    mx_koid_t pid = thread->process()->get_koid();
    mx_koid_t tid = thread->get_koid();
    LTRACEF("thread %" PRIu64 ".%" PRIu64 " gone\n", pid, tid);
    mx_exception_report_t report;
    BuildReport(&report, MX_EXCP_GONE, pid, tid);
    // The result is ignored, not much else we can do.
    SendReport(&report);
}

// This isn't called for every thread's destruction, only when a debugger
// is attached.

void ExceptionPort::OnThreadExitForDebugger(UserThread* thread) {
    canary_.Assert();

    mx_koid_t pid = thread->process()->get_koid();
    mx_koid_t tid = thread->get_koid();
    LTRACEF("thread %" PRIu64 ".%" PRIu64 " exited\n", pid, tid);
    mx_exception_report_t report;
    BuildReport(&report, MX_EXCP_THREAD_EXITING, pid, tid);
    arch_exception_context_t context;
    // There is no iframe at the moment. We'll need one (or equivalent) if/when
    // we want to make $pc, $sp available.
    memset(&context, 0, sizeof(context));
    UserThread::ExceptionStatus estatus;
    // N.B. If the process is exiting it will have killed all threads. That
    // means all threads get marked with THREAD_SIGNAL_KILL which means this
    // exchange will return immediately with ERR_INTERRUPTED.
    auto status = thread->ExceptionHandlerExchange(mxtl::RefPtr<ExceptionPort>(this), &report, &context, &estatus);
    if (status != NO_ERROR) {
        // Ignore any errors, we still want the thread to continue exiting.
    }
}
