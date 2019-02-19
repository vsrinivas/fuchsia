// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <lib/zx/port.h>
#include <lib/zx/profile.h>
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
    static zx_status_t Get(fbl::RefPtr<ThreadPool>* pool_out, uint32_t priority,
                           const char* profile_name);
    static void ShutdownAll();

    void Shutdown();
    zx_status_t AddDomainToPool(fbl::RefPtr<ExecutionDomain> domain);
    void RemoveDomainFromPool(ExecutionDomain* domain);

    zx_status_t WaitOnPort(const zx::handle& handle,
                           uint64_t key,
                           zx_signals_t signals,
                           uint32_t options);
    zx_status_t CancelWaitOnPort(const zx::handle& handle, uint64_t key);
    zx_status_t BindIrqToPort(const zx::handle& irq_handle, uint64_t key);

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
        // primitives instead.
        //
        // TODO(johngro) : What is the proper "invalid" value to initialize with
        // here?
        thrd_t thread_handle_;
        fbl::RefPtr<ThreadPool> pool_;
        const uint32_t id_;
    };

    ThreadPool(uint32_t priority, const char* profile_name)
        : priority_(priority), profile_name_(profile_name) {}
    ~ThreadPool() { }

    uint32_t priority() const { return priority_; }
    const char* profile_name() const { return profile_name_; }
    const zx::port& port() const { return port_; }
    const zx::profile& profile() const { return profile_; }

    void PrintDebugPrefix();
    zx_status_t Init();
    void InternalShutdown();

    struct PriorityAndProfileName {
        uint32_t priority;
        const char* profile_name;
    };

    struct PoolsTraits {
        static PriorityAndProfileName GetKey(const ThreadPool& element) {
            return PriorityAndProfileName{element.priority(), element.profile_name()};
        }
        static bool LessThan(const PriorityAndProfileName& a, const PriorityAndProfileName& b) {
            return std::tie(a.priority, a.profile_name) < std::tie(b.priority, b.profile_name);
        }
        static bool EqualTo(const PriorityAndProfileName& a, const PriorityAndProfileName& b) {
            return std::tie(a.priority, a.profile_name) == std::tie(b.priority, b.profile_name);
        }
    };

    static fbl::Mutex active_pools_lock_;
    static fbl::WAVLTree<PriorityAndProfileName, fbl::RefPtr<ThreadPool>, PoolsTraits>
        active_pools_ __TA_GUARDED(active_pools_lock_);
    static bool system_shutdown_ __TA_GUARDED(active_pools_lock_);

    const uint32_t priority_;
    const char* profile_name_;
    zx::profile profile_;

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
