// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <lib/zx/port.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <threads.h>

#include <dispatcher-pool/dispatcher-execution-domain.h>

namespace dispatcher {

class ThreadPool : public fbl::RefCounted<ThreadPool>,
                   public fbl::WAVLTreeContainable<fbl::RefPtr<ThreadPool>> {
public:
    static zx_status_t Get(fbl::RefPtr<ThreadPool>* pool_out, uint32_t priority);
    static void ShutdownAll();

    void Shutdown();
    zx_status_t AddDomainToPool(fbl::RefPtr<ExecutionDomain> domain);
    void RemoveDomainFromPool(ExecutionDomain* domain);

    zx_status_t WaitOnPort(const zx::handle& handle,
                           uint64_t key,
                           zx_signals_t signals,
                           uint32_t options);
    zx_status_t CancelWaitOnPort(const zx::handle& handle, uint64_t key);

    uint32_t GetKey() const { return priority_; }

private:
    friend class fbl::RefPtr<ThreadPool>;

    class Thread : public fbl::DoublyLinkedListable<fbl::unique_ptr<Thread>> {
    public:
        static fbl::unique_ptr<Thread> Create(fbl::RefPtr<ThreadPool> pool, uint32_t id);
        zx_status_t Start();
        void Join();

    private:
        Thread(fbl::RefPtr<ThreadPool> pool, uint32_t id);

        void PrintDebugPrefix() const;
        int Main();

        // TODO(johngro) : migrate away from C11 threads, use native zircon
        // primatives instead.
        //
        // TODO(johngro) : What is the proper "invalid" value to initialize with
        // here?
        thrd_t thread_handle_;
        fbl::RefPtr<ThreadPool> pool_;
        const uint32_t id_;
    };

    explicit ThreadPool(uint32_t priority) : priority_(priority) { }
    ~ThreadPool() { }

    uint32_t priority() const { return priority_; }
    const zx::port& port() const { return port_; }

    void PrintDebugPrefix();
    zx_status_t Init();
    void InternalShutdown();

    static fbl::Mutex active_pools_lock_;
    static fbl::WAVLTree<uint32_t, fbl::RefPtr<ThreadPool>> active_pools_
        __TA_GUARDED(active_pools_lock_);
    static bool system_shutdown_ __TA_GUARDED(active_pools_lock_);

    const uint32_t priority_;

    fbl::Mutex pool_lock_ __TA_ACQUIRED_AFTER(active_pools_lock_);
    zx::port port_;
    uint32_t active_domain_count_ __TA_GUARDED(pool_lock_) = 0;
    uint32_t active_thread_count_ __TA_GUARDED(pool_lock_) = 0;
    bool pool_shutting_down_ __TA_GUARDED(pool_lock_) = false;

    fbl::DoublyLinkedList<fbl::RefPtr<ExecutionDomain>,
                           ExecutionDomain::ThreadPoolListTraits> active_domains_
        __TA_GUARDED(pool_lock_);

    fbl::DoublyLinkedList<fbl::unique_ptr<Thread>> active_threads_
        __TA_GUARDED(pool_lock_);
};

}  // namespace dispatcher
