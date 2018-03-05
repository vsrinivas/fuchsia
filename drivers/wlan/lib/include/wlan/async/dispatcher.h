// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/thread_safe_binding_set.h>
#include <zx/channel.h>


namespace wlan {
namespace async {

template <typename I>
class Dispatcher {
   public:
    explicit Dispatcher(async_t* async) : async_(async) {}

    zx_status_t AddBinding(zx::channel chan, I* intf) {
        fidl::InterfaceRequest<I> request(std::move(chan));
        bindings_.AddBinding(intf, std::move(request), async_);
        return ZX_OK;
    }


   private:
    fidl::ThreadSafeBindingSet<I> bindings_;
    async_t* async_;
};

}  // namespace async
}  // namespace wlan
