// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>
#include <mx/port.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <threads.h>

namespace audio {

class DispatcherThread : public fbl::SinglyLinkedListable<fbl::unique_ptr<DispatcherThread>> {
public:
    static mx_status_t AddClient() {
        fbl::AutoLock pool_lock(&pool_lock_);
        return AddClientLocked();
    }

    static void RemoveClient() {
        fbl::AutoLock pool_lock(&pool_lock_);
        MX_DEBUG_ASSERT(active_client_count_ > 0);
        active_client_count_--;
    }

    static void ShutdownThreadPool() {
        fbl::AutoLock pool_lock(&pool_lock_);
        ShutdownPoolLocked();
    }

    static mx::port& port() { return port_; }

    void PrintDebugPrefix() const;

private:
    friend class fbl::unique_ptr<DispatcherThread>;
    explicit DispatcherThread(uint32_t id);
    ~DispatcherThread() { }

    int Main();

    static mx_status_t AddClientLocked() __TA_REQUIRES(pool_lock_);
    static void ShutdownPoolLocked()     __TA_REQUIRES(pool_lock_);

    // TODO(johngro) : migrate away from C11 threads, use native magenta
    // primatives instead.
    //
    // TODO(johngro) : What is the proper "invalid" value to initialize with
    // here?
    thrd_t thread_;
    char name_buffer_[MX_MAX_NAME_LEN];

    static fbl::Mutex pool_lock_;
    static mx::port port_;
    static uint32_t active_client_count_ __TA_GUARDED(pool_lock_);
    static uint32_t active_thread_count_ __TA_GUARDED(pool_lock_);
    static fbl::SinglyLinkedList<fbl::unique_ptr<DispatcherThread>> thread_pool_
        __TA_GUARDED(pool_lock_);
};

}  // namespace audio
