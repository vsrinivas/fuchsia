// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>
#include <lib/zx/event.h>
#include <lib/zx/handle.h>
#include <lib/zx/port.h>
#include <fbl/function.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <unistd.h>

namespace dispatcher {

class Channel;
class ExecutionDomain;
class ThreadPool;

// class EventSource
//
// EventSource is the base class of all things which can be dispatched in the
// dispatcher framework, including Channels, Timers, and so on.
//
// All EventSources begin life after being instantiated by their derived class
// in an un-initialized state.  The transition from un-initialized to activated
// happens when the specific event source type becomes Activated.  Regardless of
// the specifics, during activation, all EventSources become associated with an
// ExecutionDomain.  Any time there is an interesting event to be dispatched, it
// will be dispatched in the execution domain which was associated with the
// EventSource at the point of activation.  When an event source is no longer
// needed, it may be Deactivated and finally destroyed.  An event source may not
// be re-activated once it has been deactivated.
//
class EventSource : public fbl::RefCounted<EventSource> {
public:
    zx_signals_t process_signal_mask()    const { return process_signal_mask_; }
    bool         InExecutionDomain()      const { return sources_node_state_.InContainer(); }
    bool         InPendingList()          const { return pending_work_node_state_.InContainer(); }

    virtual void Deactivate() __TA_EXCLUDES(obj_lock_) = 0;

    fbl::RefPtr<ExecutionDomain> ScheduleDispatch(const zx_port_packet_t& port_packet)
        __TA_EXCLUDES(obj_lock_);

protected:
    enum class DispatchState {
        Idle,
        WaitingOnPort,
        DispatchPending,
        Dispatching,
    };

    EventSource(zx_signals_t process_signal_mask);
    virtual ~EventSource();

    bool is_active() const __TA_REQUIRES(obj_lock_) { return domain_ != nullptr; }
    DispatchState dispatch_state() const __TA_REQUIRES(obj_lock_) { return dispatch_state_; }
    void InternalDeactivateLocked() __TA_REQUIRES(obj_lock_);

    zx_status_t ActivateLocked(zx::handle handle, fbl::RefPtr<ExecutionDomain> domain)
        __TA_REQUIRES(obj_lock_);
    zx_status_t WaitOnPortLocked() __TA_REQUIRES(obj_lock_);
    zx_status_t CancelPendingLocked() __TA_REQUIRES(obj_lock_);

    // Transition to the dispatching state and return true if...
    //
    // 1) We are currently in the DispatchPending state.
    // 2) We still have a domain.
    // 3) We are still in our domain's pending work queue.
    //
    // Otherwise, return false.
    bool BeginDispatching() __TA_EXCLUDES(obj_lock_);

    virtual void Dispatch(ExecutionDomain* domain) __TA_EXCLUDES(obj_lock_) = 0;

    fbl::Mutex                   obj_lock_;
    fbl::RefPtr<ExecutionDomain> domain_  __TA_GUARDED(obj_lock_);
    fbl::RefPtr<ThreadPool>      thread_pool_  __TA_GUARDED(obj_lock_);
    zx::handle                   handle_  __TA_GUARDED(obj_lock_);
    DispatchState                dispatch_state_ __TA_GUARDED(obj_lock_) = DispatchState::Idle;
    zx_port_packet_t             pending_pkt_;

private:
    friend class fbl::RefPtr<EventSource>;
    friend class ExecutionDomain;

    struct SourcesListTraits {
        static fbl::DoublyLinkedListNodeState<fbl::RefPtr<EventSource>>&
            node_state(EventSource& event_source) {
            return event_source.sources_node_state_;
        }
    };

    struct PendingWorkListTraits {
        static fbl::DoublyLinkedListNodeState<fbl::RefPtr<EventSource>>&
            node_state(EventSource& event_source) {
            return event_source.pending_work_node_state_;
        }
    };

    const zx_signals_t process_signal_mask_;

    // Node state for existing on the domain's sources_ list.
    fbl::DoublyLinkedListNodeState<fbl::RefPtr<EventSource>> sources_node_state_;

    // Node state for existing on the domain's pending_work_ list.
    fbl::DoublyLinkedListNodeState<fbl::RefPtr<EventSource>> pending_work_node_state_;
};

}  // namespace dispatcher
