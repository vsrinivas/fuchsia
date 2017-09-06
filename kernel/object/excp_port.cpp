// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/excp_port.h>

#include <err.h>
#include <inttypes.h>
#include <string.h>

#include <arch/exception.h>

#include <object/job_dispatcher.h>
#include <object/port_dispatcher.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>

#include <trace.h>

using fbl::AutoLock;

#define LOCAL_TRACE 0

static PortPacket* MakePacket(uint64_t key, uint32_t type, mx_koid_t pid, mx_koid_t tid) {
    if (!MX_PKT_IS_EXCEPTION(type))
        return nullptr;

    auto port_packet = PortDispatcher::DefaultPortAllocator()->Alloc();
    if (!port_packet)
        return nullptr;

    port_packet->packet.key = key;
    port_packet->packet.type = type | PKT_FLAG_EPHEMERAL;
    port_packet->packet.exception.pid = pid;
    port_packet->packet.exception.tid = tid;
    port_packet->packet.exception.reserved0 = 0;
    port_packet->packet.exception.reserved1 = 0;

    return port_packet;
}

// static
mx_status_t ExceptionPort::Create(Type type, fbl::RefPtr<PortDispatcher> port, uint64_t port_key,
                                  fbl::RefPtr<ExceptionPort>* out_eport) {
    fbl::AllocChecker ac;
    auto eport = new (&ac) ExceptionPort(type, fbl::move(port), port_key);
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    // ExceptionPort's ctor causes the first ref to be adopted,
    // so we should only wrap.
    *out_eport = fbl::WrapRefPtr<ExceptionPort>(eport);
    return MX_OK;
}

ExceptionPort::ExceptionPort(Type type, fbl::RefPtr<PortDispatcher> port, uint64_t port_key)
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

void ExceptionPort::SetTarget(const fbl::RefPtr<JobDispatcher>& target) {
    canary_.Assert();

    LTRACE_ENTRY_OBJ;
    AutoLock lock(&lock_);
    DEBUG_ASSERT_MSG(type_ == Type::JOB,
                     "unexpected type %d", static_cast<int>(type_));
    DEBUG_ASSERT(!IsBoundLocked());
    DEBUG_ASSERT(target != nullptr);
    DEBUG_ASSERT(port_ != nullptr);
    target_ = target;
}

void ExceptionPort::SetTarget(const fbl::RefPtr<ProcessDispatcher>& target) {
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

void ExceptionPort::SetTarget(const fbl::RefPtr<ThreadDispatcher>& target) {
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

    // TODO(MG-988): Add a way to mark specific ports as unbinding quietly
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
    } else {
        switch (type_) {
            case Type::JOB: {
                DEBUG_ASSERT(target_ != nullptr);
                auto job = DownCastDispatcher<JobDispatcher>(&target_);
                DEBUG_ASSERT(job != nullptr);
                lock.release();  // The target may call our ::OnTargetUnbind
                job->ResetExceptionPort(default_quietness);
                break;
            }
            case Type::PROCESS:
            case Type::DEBUGGER: {
                DEBUG_ASSERT(target_ != nullptr);
                auto process = DownCastDispatcher<ProcessDispatcher>(&target_);
                DEBUG_ASSERT(process != nullptr);
                lock.release();  // The target may call our ::OnTargetUnbind
                process->ResetExceptionPort(type_ == Type::DEBUGGER, default_quietness);
                break;
            }
            case Type::THREAD: {
                DEBUG_ASSERT(target_ != nullptr);
                auto thread = DownCastDispatcher<ThreadDispatcher>(&target_);
                DEBUG_ASSERT(thread != nullptr);
                lock.release();  // The target may call our ::OnTargetUnbind
                thread->ResetExceptionPort(default_quietness);
                break;
            }
            default:
                PANIC("unexpected type %d", static_cast<int>(type_));
        }
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
    fbl::RefPtr<PortDispatcher> port;
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

mx_status_t ExceptionPort::SendPacketWorker(uint32_t type, mx_koid_t pid, mx_koid_t tid) {
    AutoLock lock(&lock_);
    LTRACEF("%s, type %u, pid %" PRIu64 ", tid %" PRIu64 "\n",
            port_ == nullptr ? "Not sending exception report on unbound port"
                : "Sending exception report",
            type, pid, tid);
    if (port_ == nullptr) {
        // The port has been unbound.
        return MX_ERR_PEER_CLOSED;
    }

    auto iopk = MakePacket(port_key_, type, pid, tid);
    if (!iopk)
        return MX_ERR_NO_MEMORY;

    mx_status_t status = port_->Queue(iopk, 0, 0);
    if (status != MX_OK) {
        iopk->Free();
    }
    return status;
}

mx_status_t ExceptionPort::SendPacket(ThreadDispatcher* thread, uint32_t type) {
    canary_.Assert();

    mx_koid_t pid = thread->process()->get_koid();
    mx_koid_t tid = thread->get_koid();
    return SendPacketWorker(type, pid, tid);
}

void ExceptionPort::BuildReport(mx_exception_report_t* report, uint32_t type) {
    memset(report, 0, sizeof(*report));
    report->header.size = sizeof(*report);
    report->header.type = type;
}

void ExceptionPort::BuildArchReport(mx_exception_report_t* report, uint32_t type,
                                    const arch_exception_context_t* arch_context) {
    BuildReport(report, type);
    arch_fill_in_exception_context(arch_context, report);
}

void ExceptionPort::OnThreadStart(ThreadDispatcher* thread) {
    canary_.Assert();

    mx_koid_t pid = thread->process()->get_koid();
    mx_koid_t tid = thread->get_koid();
    LTRACEF("thread %" PRIu64 ".%" PRIu64 " started\n", pid, tid);

    mx_exception_report_t report;
    BuildReport(&report, MX_EXCP_THREAD_STARTING);
    arch_exception_context_t context;
    // There is no iframe at the moment. We'll need one (or equivalent) if/when
    // we want to make $pc, $sp available.
    memset(&context, 0, sizeof(context));
    ThreadDispatcher::ExceptionStatus estatus;
    auto status = thread->ExceptionHandlerExchange(fbl::RefPtr<ExceptionPort>(this), &report, &context, &estatus);
    if (status != MX_OK) {
        // Ignore any errors. There's nothing we can do here, and
        // we still want the thread to run. It's possible the thread was
        // killed (status == MX_ERR_INTERNAL_INTR_KILLED), the kernel will kill the
        // thread shortly.
    }
}

void ExceptionPort::OnThreadSuspending(ThreadDispatcher* thread) {
    canary_.Assert();

    mx_koid_t pid = thread->process()->get_koid();
    mx_koid_t tid = thread->get_koid();
    LTRACEF("thread %" PRIu64 ".%" PRIu64 " suspending\n", pid, tid);

    // A note on the tense of the words used here: suspending vs suspended.
    // "suspending" is used in the internal context because we're still
    // in the process of suspending the thread. "suspended" is used in the
    // external context because once the debugger receives the "suspended"
    // report it can assume the thread is, for its purposes, suspended.

    // The result is ignored, not much else we can do.
    SendPacket(thread, MX_EXCP_THREAD_SUSPENDED);
}

void ExceptionPort::OnThreadResuming(ThreadDispatcher* thread) {
    canary_.Assert();

    mx_koid_t pid = thread->process()->get_koid();
    mx_koid_t tid = thread->get_koid();
    LTRACEF("thread %" PRIu64 ".%" PRIu64 " resuming\n", pid, tid);

    // See OnThreadSuspending for a note on the tense of the words uses here:
    // suspending vs suspended.

    // The result is ignored, not much else we can do.
    SendPacket(thread, MX_EXCP_THREAD_RESUMED);
}

// This isn't called for every process's destruction, only for processes that
// have a bound process or debugger exception export.

void ExceptionPort::OnProcessExit(ProcessDispatcher* process) {
    canary_.Assert();

    mx_koid_t pid = process->get_koid();
    LTRACEF("process %" PRIu64 " gone\n", pid);

    // The result is ignored, not much else we can do.
    SendPacketWorker(MX_EXCP_GONE, pid, MX_KOID_INVALID);
}

// This isn't called for every thread's destruction, only for threads that
// have a thread-specific exception handler.

void ExceptionPort::OnThreadExit(ThreadDispatcher* thread) {
    canary_.Assert();

    mx_koid_t pid = thread->process()->get_koid();
    mx_koid_t tid = thread->get_koid();
    LTRACEF("thread %" PRIu64 ".%" PRIu64 " gone\n", pid, tid);

    // The result is ignored, not much else we can do.
    SendPacket(thread, MX_EXCP_GONE);
}

// This isn't called for every thread's destruction, only when a debugger
// is attached.

void ExceptionPort::OnThreadExitForDebugger(ThreadDispatcher* thread) {
    canary_.Assert();

    mx_koid_t pid = thread->process()->get_koid();
    mx_koid_t tid = thread->get_koid();
    LTRACEF("thread %" PRIu64 ".%" PRIu64 " exited\n", pid, tid);

    mx_exception_report_t report;
    BuildReport(&report, MX_EXCP_THREAD_EXITING);
    arch_exception_context_t context;
    // There is no iframe at the moment. We'll need one (or equivalent) if/when
    // we want to make $pc, $sp available.
    memset(&context, 0, sizeof(context));
    ThreadDispatcher::ExceptionStatus estatus;
    // N.B. If the process is exiting it will have killed all threads. That
    // means all threads get marked with THREAD_SIGNAL_KILL which means this
    // exchange will return immediately with MX_ERR_INTERNAL_INTR_KILLED.
    auto status = thread->ExceptionHandlerExchange(fbl::RefPtr<ExceptionPort>(this), &report, &context, &estatus);
    if (status != MX_OK) {
        // Ignore any errors, we still want the thread to continue exiting.
    }
}
