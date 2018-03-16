// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>
#include <lib/zx/event.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include <dispatcher-pool/dispatcher-event-source.h>

namespace dispatcher {

class ThreadPool;

// class ExecutionDomain
//
// In the dispatcher framework, ExecutionDomains represent a context which
// specific types of EventSources become bound to during activation.  While many
// EventSources may have interesting things happening on them simultaniously,
// the ExecutionDomain they are bound to guarantees that only EventSource's
// handler will be executed at any given point in time.
//
// Once created using the static Create method, an ExecutionDomain is
// immediately Active.  New EventSources may be activated on it until the domain
// becomes deactivated, at which point new attempts to activate an event source
// on the execution domain will fail.
//
// Deactivating an execution domain will automatically deactivate all event
// sources currently bound to the domain.  When deactivated from outside of the
// context of a dispatch operation, callers use the Deactivate method and will
// be synchronized with any currently in-flight dispatch operations.  IOW -
// after Deactivate() returns, all dispatch operatons are guaranteed to be
// finished and no new dispatch operations will be started.
//
// If an execution domain needs to be deactivated from the context of a dispatch
// operations, the DeactivateFromWithinDomain method is used instead.  No new
// dispatch operations will be started, and the system will be completely
// deactivated when the current in-flight dispatch operation unwinds.
//
class ExecutionDomain : public fbl::RefCounted<ExecutionDomain> {
public:
    // Token and ScopedToken are small (empty) objects which are intended to be
    // used with the clang static thread analysis framework in order to express
    // concurrency guarantees which arise from the serialized-dispatch behavior
    // provided by ExecutionsDomains.  By obtaining the capability represented
    // by an execution domain's token in an event source's processing handler,
    // users may assert at compile time that they are executing in handlers in a
    // serialized fashion imposed by a particular execution domain.  For
    // example...
    //
    // class Thingy {
    //   ...
    //   // Require that we be running in my_domain_ in order to call handle
    //   // channel.
    //   void HandleChannel(Channel* ch) __TA_REQUIRES(my_domain_.token());
    //   ...
    //
    //   // This variable can only be changes while running in my_domain_
    //   uint32_t my_state_ __TA_GUARDED(my_domain_.token());
    // }
    //
    // void Thingy::Activate() {
    //     Channel::ProcessHandler phandler(
    //     [thingy = fbl::WrapRefPtr(this)](Channel* ch) {
    //       // Establish the fact that this callback is running in my_domain_
    //       ExecutionDomain::ScopedToken token(my_domain_->token());
    //       thingy->HandleChannel(ch);
    //     });
    //
    //     my_channel_.Activate(..., fbl::move(phandler), ...);
    //
    //     my_state_++;  // This fails, we are not running in the domain.
    // }
    //
    // void Thingy::HandleChannel(Channel* ch) {
    //     my_state_++;  // This succeeds, we are running in the domain.
    //     DeactivateFromWithinDomain(); // So does this.
    //
    //     // This fails, we should call not Deactivate from within the domain.
    //     Deactivate();
    // }
    //
    //
    struct __TA_CAPABILITY("role") Token { };
    class __TA_SCOPED_CAPABILITY ScopedToken {
    public:
        explicit ScopedToken(const Token& token) __TA_ACQUIRE(token) { }
        ~ScopedToken() __TA_RELEASE() { }
    };

    static constexpr uint32_t DEFAULT_PRIORITY = 16;
    static fbl::RefPtr<ExecutionDomain> Create(uint32_t priority = DEFAULT_PRIORITY);

    void Deactivate() __TA_EXCLUDES(domain_token_) { Deactivate(true); }
    void DeactivateFromWithinDomain() __TA_REQUIRES(domain_token_) { Deactivate(false); }

    bool deactivated() const __TA_NO_THREAD_SAFETY_ANALYSIS {
        return (deactivated_.load() != 0);
    }

    const Token& token() __TA_RETURN_CAPABILITY(domain_token_) { return domain_token_; }

private:
    friend class fbl::RefPtr<ExecutionDomain>;
    friend class Channel;
    friend class EventSource;
    friend class Thread;
    friend class ThreadPool;
    friend class Timer;
    friend class WakeupEvent;
    using DispatchState = EventSource::DispatchState;

    struct ThreadPoolListTraits {
        static fbl::DoublyLinkedListNodeState<fbl::RefPtr<ExecutionDomain>>&
            node_state(ExecutionDomain& domain) {
            return domain.thread_pool_node_state_;
        }
    };

    ExecutionDomain(fbl::RefPtr<ThreadPool> thread_pool, zx::event dispatch_idle_evt);
    virtual ~ExecutionDomain();

    void Deactivate(bool sync_dispatch);

    fbl::RefPtr<ThreadPool> GetThreadPool() __TA_EXCLUDES(sources_lock_);
    zx_status_t AddEventSource(fbl::RefPtr<EventSource>&& source)
        __TA_EXCLUDES(sources_lock_);
    void RemoveEventSource(EventSource* source)
        __TA_EXCLUDES(sources_lock_);

    // Add an event source which has pending work to the queue of pending
    // work for this owner.  Returns true if this was the first pending job
    // added to the queue, and therefor the calling thread is responsible
    // for processing the contents of the queue now.
    bool AddPendingWork(EventSource* source)
        __TA_REQUIRES(source->obj_lock_) __TA_EXCLUDES(sources_lock_);

    // Attempt to remove an event source from this owner's pending work
    // list.  Returns true if the source was a member of the list and was
    // removed, false otherwise.
    bool RemovePendingWork(EventSource* source)
        __TA_REQUIRES(source->obj_lock_) __TA_EXCLUDES(sources_lock_);

    // Process the pending work queue.
    void DispatchPendingWork();

    fbl::Mutex sources_lock_;
    Token domain_token_;
    fbl::atomic<uint32_t> deactivated_ __TA_GUARDED(sources_lock_);
    bool dispatch_in_progress_ __TA_GUARDED(sources_lock_) = false;
    bool dispatch_sync_in_progress_ __TA_GUARDED(sources_lock_) = false;
    fbl::RefPtr<ThreadPool> thread_pool_ __TA_GUARDED(sources_lock_);
    zx::event dispatch_idle_evt_;

    // The list of all sources bound to us, as well as the sources which are
    // currently waiting to be dispatched.
    fbl::DoublyLinkedList<fbl::RefPtr<EventSource>,
                           EventSource::SourcesListTraits> sources_
                               __TA_GUARDED(sources_lock_);
    fbl::DoublyLinkedList<fbl::RefPtr<EventSource>,
                           EventSource::PendingWorkListTraits> pending_work_
                               __TA_GUARDED(sources_lock_);

    // Node state for existing in our thread pool's execution domain list.
    fbl::DoublyLinkedListNodeState<fbl::RefPtr<ExecutionDomain>> thread_pool_node_state_;
};

// A helper macro which can ease so of the namespace pain of establishing the
// fact that you are running in a particular execution domain.  Instead of
// saying something like this...
//
// ::dispatcher::ExecutionDomain::ScopedToken token(my_domain_->token());
//
// One can also say...
//
// OBTAIN_EXECUTION_DOMAIN_TOKEN(token, my_domain_);
//
#define OBTAIN_EXECUTION_DOMAIN_TOKEN(_sym_name, _exe_domain) \
    ::dispatcher::ExecutionDomain::ScopedToken _sym_name((_exe_domain)->token())

}  // namespace dispatcher
