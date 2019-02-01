// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mutex>

#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/async/task.h>
#include <lib/fidl/cpp/thread_safe_binding_set.h>
#include <lib/fit/function.h>
#include <zx/channel.h>
#include <zx/time.h>

namespace wlan {
namespace async {

template <typename I> class Dispatcher {
   public:
    Dispatcher(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

    // Start serving requests on the given channel.
    // Fails if shutdown has been initiated.
    zx_status_t AddBinding(zx::channel chan, I* intf) {
        std::lock_guard<std::mutex> shutdown_guard(lock_);
        if (shutdown_initiated_) { return ZX_ERR_PEER_CLOSED; }
        fidl::InterfaceRequest<I> request(std::move(chan));
        bindings_.AddBinding(intf, std::move(request), dispatcher_);
        return ZX_OK;
    }

    // Stop accepting new requests initiate shutdown.
    // If |ready_callback| is supplied, then it will be called from
    // the event loop thread once shutdown is complete.
    //
    // If |InitiateShutdown| has been already called previously,
    // then it returns immediately, and |ready_callback| is ignored.
    void InitiateShutdown(fit::closure ready_callback) {
        {
            std::lock_guard<std::mutex> guard(lock_);
            if (shutdown_initiated_) { return; }
            shutdown_initiated_ = true;
        }

        // Release any FIDL bindings and close their channels. This ensures
        // that no additional requests will be made via this dispatcher
        bindings_.CloseAll();

        // Submit a sentinel task. Since the event loop in our async_t is single-threaded,
        // the execution of this task will guarantee that all in-flight requests have finished.
        if (ready_callback) {
            zx_status_t status = ::async::PostTask(dispatcher_, std::move(ready_callback));
            ZX_DEBUG_ASSERT(status == ZX_OK);
        }
    }

   private:
    fidl::ThreadSafeBindingSet<I> bindings_;
    async_dispatcher_t* dispatcher_;
    std::mutex lock_;
    bool shutdown_initiated_ __TA_GUARDED(lock_){false};
};

}  // namespace async
}  // namespace wlan
