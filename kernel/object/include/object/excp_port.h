// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <kernel/mutex.h>
#include <object/dispatcher.h>
#include <object/exception.h>

#include <magenta/syscalls/exception.h>
#include <magenta/syscalls/port.h>
#include <magenta/types.h>
#include <mxtl/auto_lock.h>
#include <mxtl/canary.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/mutex.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>

class ThreadDispatcher;
class ProcessDispatcher;
class PortDispatcher;

// Represents the binding of an exception port to a specific target
// (system/process/thread). Multiple ExceptionPorts may exist for a
// single underlying PortDispatcher.
class ExceptionPort : public mxtl::DoublyLinkedListable<mxtl::RefPtr<ExceptionPort>>
                    , public mxtl::RefCounted<ExceptionPort> {
public:
    enum class Type { NONE, DEBUGGER, THREAD, PROCESS, JOB};

    static mx_status_t Create(Type type, mxtl::RefPtr<PortDispatcher> port,
                              uint64_t port_key,
                              mxtl::RefPtr<ExceptionPort>* eport);
    ~ExceptionPort();

    Type type() const { return type_; }

    mx_status_t SendPacket(ThreadDispatcher* thread, uint32_t type);

    void OnThreadStart(ThreadDispatcher* thread);

    void OnThreadSuspending(ThreadDispatcher* thread);
    void OnThreadResuming(ThreadDispatcher* thread);

    void OnProcessExit(ProcessDispatcher* process);
    void OnThreadExit(ThreadDispatcher* thread);
    void OnThreadExitForDebugger(ThreadDispatcher* thread);

    // Records the target that the ExceptionPort is bound to, so it can
    // unbind when the underlying PortDispatcher dies.
    void SetTarget(const mxtl::RefPtr<JobDispatcher>& target);
    void SetTarget(const mxtl::RefPtr<ProcessDispatcher>& target);
    void SetTarget(const mxtl::RefPtr<ThreadDispatcher>& target);

    // Drops the ExceptionPort's references to its target and PortDispatcher.
    // Called by the target when the port is explicitly unbound.
    void OnTargetUnbind();

    static void BuildArchReport(mx_exception_report_t* report, uint32_t type,
                                const arch_exception_context_t* arch_context);

private:
    friend class PortDispatcher;

    ExceptionPort(Type type, mxtl::RefPtr<PortDispatcher> port, uint64_t port_key);

    ExceptionPort(const ExceptionPort&) = delete;
    ExceptionPort& operator=(const ExceptionPort&) = delete;

    mx_status_t SendPacketWorker(uint32_t type, mx_koid_t pid, mx_koid_t tid);

    // Unbinds from the target if bound, and drops the ref to |port_|.
    // Called by |port_| when it reaches zero handles.
    void OnPortZeroHandles();

#if DEBUG_ASSERT_IMPLEMENTED
    // Lets PortDispatcher assert that this eport is associated
    // with the right instance.
    bool PortMatches(const PortDispatcher *port, bool allow_null) {
        mxtl::AutoLock lock(&lock_);
        return (allow_null && port_ == nullptr) || port_.get() == port;
    }
#endif  // if DEBUG_ASSERT_IMPLEMENTED

    // Returns true if the ExceptionPort is currently bound to a target.
    bool IsBoundLocked() const TA_REQ(lock_) {
        return target_ != nullptr;
    }

    static void BuildReport(mx_exception_report_t* report, uint32_t type);

    mxtl::Canary<mxtl::magic("EXCP")> canary_;

    // These aren't locked as once the exception port is created these are
    // immutable (the port itself has its own locking though).
    const Type type_;
    const uint64_t port_key_;

    // The underlying port. If null, the ExceptionPort has been unbound.
    mxtl::RefPtr<PortDispatcher> port_ TA_GUARDED(lock_);

    // The target of the exception port.
    // The system exception port doesn't have a Dispatcher, hence the bool.
    mxtl::RefPtr<Dispatcher> target_ TA_GUARDED(lock_);

    mxtl::Mutex lock_;

    // NOTE: The DoublyLinkedListNodeState is guarded by |port_|'s lock,
    // and should only be touched using port_->LinkExceptionPort()
    // or port_->UnlinkExceptionPort(). This goes for ::InContainer(), too.
};

// Sets the system exception port. |eport| must be non-null; use
// ResetSystemExceptionPort() to remove the currently-set port.
mx_status_t SetSystemExceptionPort(mxtl::RefPtr<ExceptionPort> eport);

// Removes the system exception port. Returns true if a port had been set.
bool ResetSystemExceptionPort();
