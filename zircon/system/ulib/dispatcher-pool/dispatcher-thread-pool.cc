// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>
#include <stdio.h>
#include <string.h>

#include <dispatcher-pool/dispatcher-execution-domain.h>
#include <dispatcher-pool/dispatcher-thread-pool.h>

#include <utility>

#include "debug-logging.h"

namespace dispatcher {

fbl::Mutex ThreadPool::active_pools_lock_;
fbl::WAVLTree<zx_koid_t, fbl::RefPtr<ThreadPool>> ThreadPool::active_pools_;
bool ThreadPool::system_shutdown_ = false;

namespace {

zx_koid_t GetKoid(const zx::profile& profile) {
    // A null profile is valid and results in the default priority thread pool,
    // which is keyed by the 0-koid.
    if (!profile.is_valid())
        return ZX_KOID_INVALID;

    zx_info_handle_basic_t profile_info = {};
    if (profile.get_info(ZX_INFO_HANDLE_BASIC, &profile_info, sizeof(profile_info), nullptr,
                         nullptr) != ZX_OK) {
        return ZX_KOID_INVALID;
    }
    return profile_info.koid;
}

} // namespace

// static
zx_status_t ThreadPool::Get(fbl::RefPtr<ThreadPool>* pool_out, zx::profile profile) {
    if (pool_out == nullptr)
        return ZX_ERR_INVALID_ARGS;

    // From here on out, we need to be inside of the active pools lock.
    fbl::AutoLock lock(&active_pools_lock_);

    // We cannot return any pool references if we are in the process of
    // system-wide shutdown.
    if (system_shutdown_)
        return ZX_ERR_BAD_STATE;

    // Do we already have a pool running at the desired priority?  If so, just
    // return a reference to it.
    auto iter = active_pools_.find(GetKoid(profile));
    if (iter.IsValid()) {
        *pool_out = iter.CopyPointer();
        return ZX_OK;
    }

    // Looks like we don't have an appropriate pool just yet.  Try to create one
    // and add it to the active set of pools.
    fbl::AllocChecker ac;
    auto new_pool = fbl::AdoptRef(new (&ac) ThreadPool(std::move(profile)));
    if (!ac.check()) {
        printf("Failed to allocate new thread pool\n");
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t res = new_pool->Init();
    if (res != ZX_OK) {
        printf("Failed to initialize new thread pool (res %d)\n", res);
        return res;
    }

    *pool_out = new_pool;
    active_pools_.insert(std::move(new_pool));
    return ZX_OK;
}

// static
void ThreadPool::ShutdownAll() {
    fbl::WAVLTree<zx_koid_t, fbl::RefPtr<ThreadPool>> shutdown_targets;

    {
        fbl::AutoLock lock(&active_pools_lock_);
        if (system_shutdown_) {
            ZX_DEBUG_ASSERT(active_pools_.is_empty());
            return;
        }

        system_shutdown_ = true;
        shutdown_targets = std::move(active_pools_);
    }

    for (auto& pool : shutdown_targets)
        pool.InternalShutdown();
}

void ThreadPool::Shutdown() {
    // If we have already been removed from the set of active thread pools, then
    // someone is already shutting us down and there is nothing to do.
    {
        fbl::AutoLock lock(&active_pools_lock_);
        if (!this->InContainer())
            return;

        active_pools_.erase(*this);
    }

    InternalShutdown();
}

zx_status_t ThreadPool::AddDomainToPool(fbl::RefPtr<ExecutionDomain> domain) {
    ZX_DEBUG_ASSERT(domain != nullptr);
    fbl::AutoLock pool_lock(&pool_lock_);

    if (pool_shutting_down_)
        return ZX_ERR_BAD_STATE;

    active_domains_.push_back(std::move(domain));
    ++active_domain_count_;

    while ((active_thread_count_ < active_domain_count_) &&
           (active_thread_count_ < zx_system_get_num_cpus())) {
        auto thread = Thread::Create(fbl::WrapRefPtr(this), active_thread_count_);
        if (thread == nullptr) {
            LOG("Failed to create new thread\n");
            break;
        }

        active_threads_.push_front(std::move(thread));
        if (active_threads_.front().Start() != ZX_OK) {
            LOG("Failed to start new thread\n");
            thread = active_threads_.pop_front();
            break;
        }

        active_thread_count_++;
    }

    return ZX_OK;
}

void ThreadPool::RemoveDomainFromPool(ExecutionDomain* domain) {
    ZX_DEBUG_ASSERT(domain != nullptr);
    fbl::AutoLock pool_lock(&pool_lock_);
    active_domains_.erase(*domain);
}

zx_status_t ThreadPool::WaitOnPort(const zx::handle& handle,
                                   uint64_t key,
                                   zx_signals_t signals,
                                   uint32_t options) {
    ZX_DEBUG_ASSERT(handle.is_valid());
    fbl::AutoLock pool_lock(&pool_lock_);

    if (!port_.is_valid()) {
        DEBUG_LOG("WaitOnPort failed, port handle is invalid\n");
        return ZX_ERR_BAD_STATE;
    }

    return handle.wait_async(port_, key, signals, options);
}

zx_status_t ThreadPool::CancelWaitOnPort(const zx::handle& handle, uint64_t key) {
    ZX_DEBUG_ASSERT(handle.is_valid());
    fbl::AutoLock pool_lock(&pool_lock_);

    if (!port_.is_valid()) {
        DEBUG_LOG("CancelWaitOnPort failed, port handle is invalid\n");
        return ZX_ERR_BAD_STATE;
    }

    return port_.cancel(handle, key);
}

zx_status_t ThreadPool::BindIrqToPort(const zx::handle& irq_handle, uint64_t key) {
    ZX_DEBUG_ASSERT(irq_handle.is_valid());
    fbl::AutoLock pool_lock(&pool_lock_);

    if (!port_.is_valid()) {
        DEBUG_LOG("BindIrqToPort failed, port handle is invalid\n");
        return ZX_ERR_BAD_STATE;
    }

    return zx_interrupt_bind(irq_handle.get(), port_.get(), key, 0u);
}

zx_koid_t ThreadPool::GetKey() const {
    return profile_koid_;
}

ThreadPool::ThreadPool(zx::profile profile) : profile_(std::move(profile)) {
    profile_koid_ = GetKoid(profile_);
}

void ThreadPool::PrintDebugPrefix() {
    printf("[ThreadPool %zu] ", GetKey());
}

zx_status_t ThreadPool::Init() {
    ZX_DEBUG_ASSERT(!port_.is_valid());

    zx_status_t res = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port_);
    if (res != ZX_OK) {
        LOG("Failed to create thread pool port (res %d)!\n", res);
        return res;
    }

    return ZX_OK;
}

void ThreadPool::InternalShutdown() {
    // Be careful when shutting down, a specific sequence needs to be followed.
    // See MG-1118 for details.
    decltype(active_domains_) domains_to_deactivate;
    {
        fbl::AutoLock lock(&pool_lock_);
        // If someone is already shutting us down, then we are done.
        if (pool_shutting_down_)
            return;

        // Prevent any new clients from joining the pool.
        pool_shutting_down_ = true;

        // Move the contents of the active domains list into a local variable so
        // that we don't need to hold onto the pool lock while we shut down the
        // domains.
        domains_to_deactivate = std::move(active_domains_);
    }

    // Deactivate any domains we may have still had assigned to us, then let go
    // of our references to them.  Deactivation of domains should synchronize
    // will all pending operations in the domain (meaning that all references
    // have been recovered and no new wait operations will be posted)
    for (auto& domain : domains_to_deactivate)
        domain.Deactivate();

    domains_to_deactivate.clear();

    // Manually queue a quit message for each thread in the thread pool.
    {
        zx_port_packet pkt;
        memset(&pkt, 0, sizeof(pkt));
        pkt.type = ZX_PKT_TYPE_USER;

        fbl::AutoLock lock(&pool_lock_);
        for (__UNUSED const auto& thread : active_threads_) {
            __UNUSED zx_status_t res;
            res = port_.queue(&pkt);
            ZX_DEBUG_ASSERT(res == ZX_OK);
        }
    }

    // Synchronize with the threads as they exit.
    while (true) {
        fbl::unique_ptr<Thread> thread;
        {
            fbl::AutoLock lock(&pool_lock_);
            if (active_threads_.is_empty())
                break;

            thread = active_threads_.pop_front();
        }

        thread->Join();
    }
}

// static
fbl::unique_ptr<ThreadPool::Thread> ThreadPool::Thread::Create(fbl::RefPtr<ThreadPool> pool,
                                                                uint32_t id) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<Thread> ret(new (&ac) Thread(std::move(pool), id));

    if (!ac.check())
        return nullptr;

    return ret;
}

ThreadPool::Thread::Thread(fbl::RefPtr<ThreadPool> pool, uint32_t id)
    : pool_(std::move(pool)),
      id_(id) {
}

zx_status_t ThreadPool::Thread::Start() {
    int c11_res = thrd_create(
            &thread_handle_,
            [](void* ctx) -> int { return static_cast<Thread*>(ctx)->Main(); },
            this);

    if (c11_res != thrd_success) {
        LOG("Failed to create new client thread (res %d)!\n", c11_res);
        // TODO(johngro) : translate C11 error
        return ZX_ERR_INTERNAL;
    }

    return ZX_OK;
}

void ThreadPool::Thread::Join() {
    // TODO(johngro) : Switch to native zircon threads so we can supply a
    // timeout to the join event.
    int musl_ret;
    thrd_join(thread_handle_, &musl_ret);
    ZX_DEBUG_ASSERT(pool_ == nullptr);
}

void ThreadPool::Thread::PrintDebugPrefix() const {
    printf("[Thread %03u-%zu] ", id_, pool_->GetKey());
}

int ThreadPool::Thread::Main() {
    zx_status_t res;

    ZX_DEBUG_ASSERT(pool_ != nullptr);
    DEBUG_LOG("Thread Starting\n");

    if (pool_->profile().is_valid()) {
        res = zx_object_set_profile(zx_thread_self(), pool_->profile().get(), 0);
        if (res != ZX_OK) {
            DEBUG_LOG("WARNING - Failed to set thread profile (res %d)\n", res);
        }
    }

    while (true) {
        zx_port_packet_t pkt;

        // TODO(johngro) : consider automatically shutting down if we have more
        // threads than clients.

        // Wait for there to be work to dispatch.  We should never encounter an
        // error, but if we do, shut down.
        res = pool_->port().wait(zx::time::infinite(), &pkt);
        ZX_DEBUG_ASSERT(res == ZX_OK);

        // Is it time to exit?
        if ((res != ZX_OK) || (pkt.type == ZX_PKT_TYPE_USER)) {
            break;
        }

        if ((pkt.type != ZX_PKT_TYPE_SIGNAL_ONE) &&
            (pkt.type != ZX_PKT_TYPE_INTERRUPT)) {
            LOG("Unexpected packet type (%u) in Thread pool!\n", pkt.type);
            continue;
        }

        // Reclaim our event source reference from the kernel.
        static_assert(sizeof(pkt.key) >= sizeof(EventSource*),
                      "Port packet keys are not large enough to hold a pointer!");
        auto event_source =
            fbl::internal::MakeRefPtrNoAdopt(reinterpret_cast<EventSource*>(pkt.key));

        // Schedule the dispatch of the pending events for this event source.
        // If ScheduleDispatch returns a valid ExecutionDomain reference, then
        // actually go ahead and perform the dispatch of pending work for this
        // domain.
        ZX_DEBUG_ASSERT(event_source != nullptr);
        fbl::RefPtr<ExecutionDomain> domain = event_source->ScheduleDispatch(pkt);

        if (domain != nullptr)
            domain->DispatchPendingWork();
    }

    DEBUG_LOG("Client work thread shutting down\n");
    pool_.reset();

    return 0;
}

}  // namespace dispatcher
