// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>
#include <zircon/syscalls/port.h>
#include <stdio.h>

#include "drivers/audio/dispatcher-pool/dispatcher-event-source.h"
#include "drivers/audio/dispatcher-pool/dispatcher-thread.h"

#include "debug-logging.h"

namespace audio {

fbl::Mutex DispatcherThread::pool_lock_;
zx::port    DispatcherThread::port_;
uint32_t    DispatcherThread::active_client_count_ = 0;
uint32_t    DispatcherThread::active_thread_count_ = 0;
fbl::SinglyLinkedList<fbl::unique_ptr<DispatcherThread>> DispatcherThread::thread_pool_;

DispatcherThread::DispatcherThread(uint32_t id) {
    // TODO(johngro) : add the process ID as part of the thread name
    snprintf(name_buffer_, countof(name_buffer_), "ihda-client-%03u", id);
}

void DispatcherThread::PrintDebugPrefix() const {
    printf("[Thread %s] ", name_buffer_);
}

zx_status_t DispatcherThread::AddClientLocked() {
    zx_status_t res = ZX_OK;

    // If we have never added any clients, we will need to start by creating the
    // central port.
    if (!port_.is_valid()) {
        res = zx::port::create(0, &port_);
        if (res != ZX_OK) {
            printf("Failed to create client therad pool port (res %d)!\n", res);
            return res;
        }
    }

    active_client_count_++;

    // Try to have as many threads as we have clients, but limit the maximum
    // number of threads to the number of cores in the system.
    //
    // TODO(johngro) : Should we allow users to have more control over the
    // maximum number of threads in the pool?
    while ((active_thread_count_ < active_client_count_) &&
           (active_thread_count_ < zx_system_get_num_cpus())) {

        fbl::unique_ptr<DispatcherThread> thread(new DispatcherThread(active_thread_count_));

        int c11_res = thrd_create(
                &thread->thread_,
                [](void* ctx) -> int { return static_cast<DispatcherThread*>(ctx)->Main(); },
                thread.get());

        if (c11_res != thrd_success) {
            printf("Failed to create new client thread (res %d)!\n", c11_res);
            active_client_count_--;
            // TODO(johngro) : translate musl error
            return ZX_ERR_INTERNAL;
        }

        active_thread_count_++;
        thread_pool_.push_front(fbl::move(thread));
    }

    return ZX_OK;
}

void DispatcherThread::ShutdownPoolLocked() {
    // Don't actually shut the pool unless the number of active clients has
    // dropped to zero.
    if (active_client_count_ > 0)
        return;

    // Have we already been shut down?
    if (!port_.is_valid()) {
        ZX_DEBUG_ASSERT(thread_pool_.is_empty());
        return;
    }

    // Close the port.  This should cause all of the threads currently waiting
    // for work to abort and shut down.
    port_.reset();

    while (!thread_pool_.is_empty()) {
        int musl_ret;
        auto thread = thread_pool_.pop_front();

        // TODO(johngro) : Switch to native zircon threads so we can supply a
        // timeout to the join event.
        thrd_join(thread->thread_, &musl_ret);
    }

    active_thread_count_ = 0;
}

int DispatcherThread::Main() {
    // TODO(johngro) : bump our thread priority to the proper level.
    while (port_.is_valid()) {
        zx_port_packet_t pkt;
        zx_status_t res;

        // Wait for there to be work to dispatch.  If we encounter an error
        // while waiting, it is time to shut down.
        //
        // TODO(johngro) : consider adding a timeout, JiC
        res = port_.wait(ZX_TIME_INFINITE, &pkt, 0);
        if (res != ZX_OK)
            break;

        if (pkt.type != ZX_PKT_TYPE_SIGNAL_ONE) {
            LOG("Unexpected packet type (%u) in DispatcherThread pool!\n", pkt.type);
            continue;
        }

        // Look of the event source which woke up this thread.  If the event
        // source is no longer in the active set, then it is in the process of
        // being torn down and this message should be ignored.
        //
        // TODO(johngro) : When we have sorted out kernel assisted ref-counting
        // of keyed objects who post activity on ports, switch to just using the
        // key of the message for O(1) lookup of the active event source,
        // instead of doing this O(log) lookup.
        auto event_source = DispatcherEventSource::GetActiveEventSource(pkt.key);
        if (event_source != nullptr) {
            zx_status_t res = ZX_OK;

            // Start by processing all of the pending messages a event_source has.
            if ((pkt.signal.observed & event_source->process_signal_mask()) != 0) {
                res = event_source->Process(pkt);
            }

            // If the event source has been signalled for shutdown, or if the
            // client ran into trouble during processing, deactivate the event
            // source.  Otherwise, if the event source has not been deactivated,
            // set up the next wait operation.
            if ((res != ZX_OK) ||
                (pkt.signal.observed & event_source->shutdown_signal_mask()) != 0) {
                if (res != ZX_OK) {
                    DEBUG_LOG("Process error (%d), deactivating event source %" PRIu64 " \n",
                              res, pkt.key);
                } else {
                    DEBUG_LOG("Peer closed, deactivating event source %" PRIu64 "\n", pkt.key);
                }
                event_source->Deactivate(true);
            } else
            if (event_source->InActiveEventSourceSet()) {
                res = event_source->WaitOnPort(port_);
                if (res != ZX_OK) {
                    DEBUG_LOG("Failed to re-arm event source wait (error %d), "
                              "deactivating event source %" PRIu64 " \n",
                              res, pkt.key);
                    event_source->Deactivate(true);
                }
            }

            // Release our event source reference.
            event_source = nullptr;
        }
    }

    DEBUG_LOG("Client work thread shutting down\n");

    return 0;
}

}  // namespace audio
