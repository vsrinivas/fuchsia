// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/event.h>

#include <dispatcher-pool/dispatcher-execution-domain.h>
#include <dispatcher-pool/dispatcher-thread-pool.h>

namespace dispatcher {

// static
fbl::RefPtr<ExecutionDomain> ExecutionDomain::Create(uint32_t priority) {
    zx::event evt;
    if (zx::event::create(0, &evt) != ZX_OK)
        return nullptr;

    if (evt.signal(0u, ZX_USER_SIGNAL_0) != ZX_OK)
        return nullptr;

    fbl::RefPtr<ThreadPool> thread_pool;
    zx_status_t res = ThreadPool::Get(&thread_pool, priority);
    if (res != ZX_OK)
        return nullptr;
    ZX_DEBUG_ASSERT(thread_pool != nullptr);

    fbl::AllocChecker ac;
    auto new_domain = fbl::AdoptRef(new (&ac) ExecutionDomain(thread_pool, fbl::move(evt)));
    if (!ac.check())
        return nullptr;

    res = thread_pool->AddDomainToPool(new_domain);
    if (res != ZX_OK)
        return nullptr;

    return fbl::move(new_domain);
}

ExecutionDomain::ExecutionDomain(fbl::RefPtr<ThreadPool> thread_pool,
                                 zx::event dispatch_idle_evt)
    : deactivated_(0),
      thread_pool_(fbl::move(thread_pool)),
      dispatch_idle_evt_(fbl::move(dispatch_idle_evt)) {
    ZX_DEBUG_ASSERT(thread_pool_ != nullptr);
    ZX_DEBUG_ASSERT(dispatch_idle_evt_.is_valid());
}

ExecutionDomain::~ExecutionDomain() {
    // Assert that the Owner implementation properly deactivated itself
    // before destructing.
    ZX_DEBUG_ASSERT(deactivated());
    ZX_DEBUG_ASSERT(sources_.is_empty());
    ZX_DEBUG_ASSERT(!thread_pool_node_state_.InContainer());
}

void ExecutionDomain::Deactivate(bool sync_dispatch) {
    // Flag ourselves as deactivated.  This will prevent any new event sources
    // from being added to the sources_ list.  We can then swap the contents of
    // the sources_ list with a temp list, leave the lock and deactivate all of
    // the sources at our leisure.
    fbl::DoublyLinkedList<fbl::RefPtr<EventSource>, EventSource::SourcesListTraits> to_deactivate;
    bool sync_needed = false;

    {
        fbl::AutoLock sources_lock(&sources_lock_);
        if (deactivated()) {
            ZX_DEBUG_ASSERT(sources_.is_empty());
        } else {
            deactivated_.store(1u);
            to_deactivate.swap(sources_);
        }

        // If there are dispatch operations currently in flight, clear the
        // dispatch idle event and set the flag indicating to the dispatch
        // operation that it needs to set the event when it finishes.
        if (dispatch_in_progress_) {
            sync_needed = true;
            if (!dispatch_sync_in_progress_) {
                __UNUSED zx_status_t res;
                dispatch_sync_in_progress_ = true;
                res = dispatch_idle_evt_.signal(ZX_USER_SIGNAL_0, 0u);
                ZX_DEBUG_ASSERT(res == ZX_OK);
            }
        }
    }

    // Now deactivate all of our event sources and release all of our references.
    if (!to_deactivate.is_empty()) {
        for (auto& source : to_deactivate) {
            source.Deactivate();
        }
        to_deactivate.clear();
    }

    // Synchronize if needed
    if (sync_needed && sync_dispatch) {
        __UNUSED zx_status_t res;
        zx_signals_t pending;

        res = dispatch_idle_evt_.wait_one(ZX_USER_SIGNAL_0, zx::deadline_after(zx::sec(5)), &pending);

        ZX_DEBUG_ASSERT(res == ZX_OK);
        ZX_DEBUG_ASSERT((pending & ZX_USER_SIGNAL_0) != 0);
    }

    // Finally, exit our thread pool and release our reference to it.
    decltype(thread_pool_) pool;
    {
        fbl::AutoLock sources_lock(&sources_lock_);
        pool = fbl::move(thread_pool_);
    }

    if (pool != nullptr)
        pool->RemoveDomainFromPool(this);
}

fbl::RefPtr<ThreadPool> ExecutionDomain::GetThreadPool() {
    fbl::AutoLock sources_lock(&sources_lock_);
    return fbl::RefPtr<ThreadPool>(thread_pool_);
}

zx_status_t ExecutionDomain::AddEventSource(
        fbl::RefPtr<EventSource>&& event_source) {
    if (event_source == nullptr)
        return ZX_ERR_INVALID_ARGS;

    // This check is a bit sketchy...  This event_source should *never* be in
    // any ExecutionDomain's event_source list at this point in time, however if
    // it is, we don't really know what lock we need to obtain to make this
    // observation atomically.  That said, the check will not mutate any state,
    // so it should be safe.  It just might not catch a bad situation which
    // should never happen.
    ZX_DEBUG_ASSERT(!event_source->InExecutionDomain());

    // If this ExecutionDomain has become deactivated, then it is not accepting
    // any new event sources.  Fail the request to add this event_source.
    fbl::AutoLock sources_lock(&sources_lock_);
    if (deactivated())
        return ZX_ERR_BAD_STATE;

    // We are still active.  Transfer the reference to this event_source to our set
    // of sources.
    sources_.push_front(fbl::move(event_source));
    return ZX_OK;
}

void ExecutionDomain::RemoveEventSource(EventSource* event_source) {
    fbl::AutoLock sources_lock(&sources_lock_);

    // Has this ExecutionDomain become deactivated?  If so, then this
    // event_source may still be on a list (the local 'to_deactivate' list in
    // Deactivate), but it is not in the ExecutionDomain's sources_ list, so
    // there is nothing to do here.
    if (deactivated()) {
        ZX_DEBUG_ASSERT(sources_.is_empty());
        return;
    }

    // If the event_source has not already been removed from the domain's list, do
    // so now.
    if (event_source->InExecutionDomain())
        sources_.erase(*event_source);
}

bool ExecutionDomain::AddPendingWork(EventSource* event_source) {
    ZX_DEBUG_ASSERT(event_source != nullptr);
    ZX_DEBUG_ASSERT(!event_source->InPendingList());
    ZX_DEBUG_ASSERT(event_source->dispatch_state() == DispatchState::WaitingOnPort);

    // If this ExecutionDomain has become deactivated, then it is not accepting
    // any new pending work.   Do not add the source to the pending work queue,
    // and do not tell the caller that it should be processing the queue when we
    // return.  The event source is now in the Idle state.
    fbl::AutoLock sources_lock(&sources_lock_);
    if (deactivated()) {
        event_source->dispatch_state_ = DispatchState::Idle;
        return false;
    }

    // Add this event source to the back of the pending work queue, and tell the
    // caller whether or not it is responsible for processing the queue.
    bool ret = !dispatch_in_progress_;
    if (ret) {
        ZX_DEBUG_ASSERT(pending_work_.is_empty());
        dispatch_in_progress_ = true;
    }

    event_source->dispatch_state_ = DispatchState::DispatchPending;
    pending_work_.push_back(fbl::WrapRefPtr(event_source));

    return ret;
}

bool ExecutionDomain::RemovePendingWork(EventSource* event_source) {
    ZX_DEBUG_ASSERT(event_source != nullptr);

    fbl::AutoLock sources_lock(&sources_lock_);
    if (!event_source->InPendingList())
        return false;

    // If we were on the pending list, then our state must be DispatchPending;
    ZX_DEBUG_ASSERT(event_source->dispatch_state() == DispatchState::DispatchPending);
    pending_work_.erase(*event_source);
    return true;
}

void ExecutionDomain::DispatchPendingWork() {
    // While we have work waiting in the pending queue, dispatch it.
    //
    // TODO(johngro) : To prevent starvation issues, we should probably only
    // perform a finite amount of work, and unwind out into the port wait
    // operation to give other event source owners a chance if this ends up
    // going on for too long.
    while (true) {
        // Enter the sources lock and take a reference to the front of the
        // pending queue.  If the pending work queue is empty, or we have been
        // deactivated, we are finished.
        fbl::RefPtr<EventSource> source;
        {
            fbl::AutoLock sources_lock(&sources_lock_);
            ZX_DEBUG_ASSERT(dispatch_in_progress_);
            if (deactivated() || pending_work_.is_empty()) {
                // Clear the pending work queue and the dispatch in progress
                // flag.  If someone is attempting to synchronize with dispatch
                // operations in flight, set the event indicating that we are
                // now idle.
                pending_work_.clear();
                dispatch_in_progress_ = false;
                if (dispatch_sync_in_progress_) {
                    __UNUSED zx_status_t res;
                    res = dispatch_idle_evt_.signal(0u, ZX_USER_SIGNAL_0);
                    ZX_DEBUG_ASSERT(res == ZX_OK);
                }
                return;
            }

            source = pending_work_.begin().CopyPointer();
        }

        // Attempt to transition to the Dispatching state.  If this fails, it
        // means that we were canceled after we left the sources_lock_ but
        // before we managed to re-enter both the EventSource's object lock and
        // the execution domain's sources lock.  If this is the case, just move
        // on to the next pending source.
        ZX_DEBUG_ASSERT(source != nullptr);
        if (source->BeginDispatching())
            source->Dispatch(this);
    }
}

}  // namespace dispatcher
