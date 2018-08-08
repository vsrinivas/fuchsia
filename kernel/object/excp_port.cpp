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

static PortPacket* MakePacket(uint64_t key, uint32_t type, zx_koid_t pid, zx_koid_t tid) {
    if (!ZX_PKT_IS_EXCEPTION(type))
        return nullptr;

    auto port_packet = PortDispatcher::DefaultPortAllocator()->Alloc();
    if (!port_packet)
        return nullptr;

    port_packet->packet.key = key;
    port_packet->packet.type = type;
    port_packet->packet.status = ZX_OK;
    port_packet->packet.exception.pid = pid;
    port_packet->packet.exception.tid = tid;
    port_packet->packet.exception.reserved0 = 0;
    port_packet->packet.exception.reserved1 = 0;

    return port_packet;
}

// static
zx_status_t ExceptionPort::Create(Type type, fbl::RefPtr<PortDispatcher> port, uint64_t port_key,
                                  fbl::RefPtr<ExceptionPort>* out_eport) {
    fbl::AllocChecker ac;
    auto eport = new (&ac) ExceptionPort(type, fbl::move(port), port_key);
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    // ExceptionPort's ctor causes the first ref to be adopted,
    // so we should only wrap.
    *out_eport = fbl::WrapRefPtr<ExceptionPort>(eport);
    return ZX_OK;
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

    // TODO(ZX-988): Add a way to mark specific ports as unbinding quietly
    // when auto-unbinding.
    static const bool default_quietness = false;

    LTRACE_ENTRY_OBJ;
    AutoLock lock(&lock_);
    if (port_ == nullptr) {
        // Already unbound. This can happen when
        // PortDispatcher::on_zero_handles and a manual unbind (via
        // zx_task_bind_exception_port) race with each other.
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
            // via zx_task_bind_exception_port.
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

bool ExceptionPort::PortMatches(const PortDispatcher *port, bool allow_null) {
    fbl::AutoLock lock(&lock_);
    return (allow_null && port_ == nullptr) || port_.get() == port;
}

zx_status_t ExceptionPort::SendPacketWorker(uint32_t type, zx_koid_t pid, zx_koid_t tid) {
    AutoLock lock(&lock_);
    LTRACEF("%s, type %u, pid %" PRIu64 ", tid %" PRIu64 "\n",
            port_ == nullptr ? "Not sending exception report on unbound port"
                : "Sending exception report",
            type, pid, tid);
    if (port_ == nullptr) {
        // The port has been unbound.
        return ZX_ERR_PEER_CLOSED;
    }

    auto iopk = MakePacket(port_key_, type, pid, tid);
    if (!iopk)
        return ZX_ERR_NO_MEMORY;

    zx_status_t status = port_->Queue(iopk, 0, 0);
    if (status != ZX_OK) {
        iopk->Free();
    }
    return status;
}

zx_status_t ExceptionPort::SendPacket(ThreadDispatcher* thread, uint32_t type) {
    canary_.Assert();

    zx_koid_t pid = thread->process()->get_koid();
    zx_koid_t tid = thread->get_koid();
    return SendPacketWorker(type, pid, tid);
}

void ExceptionPort::BuildReport(zx_exception_report_t* report, uint32_t type) {
    memset(report, 0, sizeof(*report));
    report->header.size = sizeof(*report);
    report->header.type = type;
}

void ExceptionPort::BuildArchReport(zx_exception_report_t* report, uint32_t type,
                                    const arch_exception_context_t* arch_context) {
    BuildReport(report, type);
    arch_fill_in_exception_context(arch_context, report);
}

void ExceptionPort::OnThreadStartForDebugger(ThreadDispatcher* thread) {
    canary_.Assert();

    DEBUG_ASSERT(type_ == Type::DEBUGGER);

    zx_koid_t pid = thread->process()->get_koid();
    zx_koid_t tid = thread->get_koid();
    LTRACEF("thread %" PRIu64 ".%" PRIu64 " started\n", pid, tid);

    zx_exception_report_t report;
    BuildReport(&report, ZX_EXCP_THREAD_STARTING);
    arch_exception_context_t context;
    // There is no iframe at the moment. We'll need one (or equivalent) if/when
    // we want to make $pc, $sp available.
    memset(&context, 0, sizeof(context));
    ThreadDispatcher::ExceptionStatus estatus;
    auto status = thread->ExceptionHandlerExchange(fbl::RefPtr<ExceptionPort>(this), &report, &context, &estatus);
    if (status != ZX_OK) {
        // Ignore any errors. There's nothing we can do here, and
        // we still want the thread to run. It's possible the thread was
        // killed (status == ZX_ERR_INTERNAL_INTR_KILLED), the kernel will kill the
        // thread shortly.
    }
}

// This isn't called for every thread's destruction, only when a debugger
// is attached.

void ExceptionPort::OnThreadExitForDebugger(ThreadDispatcher* thread) {
    canary_.Assert();

    DEBUG_ASSERT(type_ == Type::DEBUGGER);

    zx_koid_t pid = thread->process()->get_koid();
    zx_koid_t tid = thread->get_koid();
    LTRACEF("thread %" PRIu64 ".%" PRIu64 " exited\n", pid, tid);

    zx_exception_report_t report;
    BuildReport(&report, ZX_EXCP_THREAD_EXITING);
    arch_exception_context_t context;
    // There is no iframe at the moment. We'll need one (or equivalent) if/when
    // we want to make $pc, $sp available.
    memset(&context, 0, sizeof(context));
    ThreadDispatcher::ExceptionStatus estatus;
    // N.B. If the process is exiting it will have killed all threads. That
    // means all threads get marked with THREAD_SIGNAL_KILL which means this
    // exchange will return immediately with ZX_ERR_INTERNAL_INTR_KILLED.
    auto status = thread->ExceptionHandlerExchange(fbl::RefPtr<ExceptionPort>(this), &report, &context, &estatus);
    if (status != ZX_OK) {
        // Ignore any errors, we still want the thread to continue exiting.
    }
}
