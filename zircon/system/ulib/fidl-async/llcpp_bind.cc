// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/transaction.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl-async/cpp/channel_transaction.h>
#include <lib/zx/channel.h>
#include <zircon/syscalls.h>

#include <type_traits>

namespace fidl {

namespace internal {

SimpleBinding::SimpleBinding(async_dispatcher_t* dispatcher,
                             zx::channel channel,
                             void* impl,
                             TypeErasedDispatchFn dispatch_fn)
    : async_wait_t({
          .state = ASYNC_STATE_INIT,
          .handler = &MessageHandler,
          .object = channel.release(),
          .trigger = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED
      }),
    dispatcher_(dispatcher), interface_(impl), dispatch_fn_(dispatch_fn) {
}

SimpleBinding::~SimpleBinding() {
    zx_handle_close(async_wait_t::object);
}

void SimpleBinding::MessageHandler(async_dispatcher_t* dispatcher,
                                   async_wait_t* wait,
                                   zx_status_t status,
                                   const zx_packet_signal_t* signal) {
    std::unique_ptr<SimpleBinding> binding(static_cast<SimpleBinding*>(wait));
    if (status != ZX_OK) {
        return;
    }

    if (signal->observed & ZX_CHANNEL_READABLE) {
        char bytes[ZX_CHANNEL_MAX_MSG_BYTES];
        zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
        for (uint64_t i = 0; i < signal->count; i++) {
            fidl_msg_t msg = {
                .bytes = bytes,
                .handles = handles,
                .num_bytes = 0u,
                .num_handles = 0u,
            };
            status = zx_channel_read(wait->object, 0, bytes, handles,
                                     ZX_CHANNEL_MAX_MSG_BYTES,
                                     ZX_CHANNEL_MAX_MSG_HANDLES,
                                     &msg.num_bytes, &msg.num_handles);
            if (status != ZX_OK || msg.num_bytes < sizeof(fidl_message_header_t)) {
                return;
            }

            auto hdr = reinterpret_cast<fidl_message_header_t*>(msg.bytes);
            ChannelTransaction txn(hdr->txid, std::move(binding));
            txn.Dispatch(msg);
            binding = txn.TakeBinding();
            if (!binding) {
                return;
            }
        }

        // Will only get here if every single message was handled synchronously and successfully.
        BeginWait(&binding);
    } else {
        ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
    }
}

zx_status_t BeginWait(std::unique_ptr<SimpleBinding>* unique_binding) {
    SimpleBinding* binding = unique_binding->release();
    zx_status_t status;
    if ((status = async_begin_wait(binding->dispatcher_, binding)) != ZX_OK) {
        // Failed to transfer binding ownership to async dispatcher.
        unique_binding->reset(binding);
    }
    return status;
}

zx_status_t TypeErasedBind(async_dispatcher_t* dispatcher,
                           zx::channel channel,
                           void* impl,
                           TypeErasedDispatchFn dispatch_fn) {
    auto binding = std::make_unique<SimpleBinding>(dispatcher,
                                                   std::move(channel),
                                                   impl,
                                                   dispatch_fn);
    auto status = BeginWait(&binding);
    return status;
}

} // namespace internal

} // namespace fidl
