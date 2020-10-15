// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/trace.h>
#include <lib/fidl/txn_header.h>
#include <lib/fit/function.h>
#include <stdio.h>

namespace fidl {
namespace internal {

// TODO(madhaviyengar): Move this constant to zircon/fidl.h
constexpr uint32_t kUserspaceTxidMask = 0x7FFFFFFF;

zx_status_t ClientBase::Bind(std::shared_ptr<ClientBase> client, zx::channel channel,
                             async_dispatcher_t* dispatcher, OnClientUnboundFn on_unbound) {
  ZX_DEBUG_ASSERT(!binding_.lock());
  ZX_DEBUG_ASSERT(client.get() == this);
  channel_tracker_.Init(std::move(channel));
  auto binding = AsyncClientBinding::Create(dispatcher, channel_tracker_.Get(), std::move(client),
                                            std::move(on_unbound));
  auto status = binding->BeginWait();
  binding_ = std::move(binding);
  return status;
}

void ClientBase::Unbind() {
  if (auto binding = binding_.lock())
    binding->Unbind(std::move(binding));
}

zx::channel ClientBase::WaitForChannel() {
  // Unbind to release the AsyncClientBinding's reference to the channel.
  Unbind();
  // Wait for all references to be released.
  return channel_tracker_.WaitForChannel();
}

void ClientBase::PrepareAsyncTxn(ResponseContext* context) {
  std::scoped_lock lock(lock_);

  // Generate the next txid. Verify that it doesn't overlap with any outstanding txids.
  do {
    do {
      context->txid_ = ++txid_base_ & kUserspaceTxidMask;  // txid must be within mask.
    } while (!context->txid_);                             // txid must be non-zero.
  } while (contexts_.find(context->txid_) != contexts_.end());

  // Insert the ResponseContext.
  contexts_.insert(context);
  list_add_tail(&delete_list_, context);
}

void ClientBase::ForgetAsyncTxn(ResponseContext* context) {
  std::scoped_lock lock(lock_);

  ZX_ASSERT(context->InContainer());
  contexts_.erase(*context);
  list_delete(static_cast<list_node_t*>(context));
}

void ClientBase::ReleaseResponseContextsWithError() {
  // Invoke OnError() on any outstanding ResponseContexts outside of locks.
  list_node_t delete_list;
  {
    std::scoped_lock lock(lock_);
    contexts_.clear();
    list_move(&delete_list_, &delete_list);
  }
  list_node_t* node = nullptr;
  list_node_t* temp_node = nullptr;
  list_for_every_safe(&delete_list, node, temp_node) {
    list_delete(node);
    static_cast<ResponseContext*>(node)->OnError();
  }
}

std::optional<UnbindInfo> ClientBase::Dispatch(fidl_incoming_msg_t* msg) {
  auto* hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);

  if (hdr->ordinal == kFidlOrdinalEpitaph) {
    zx_handle_close_many(msg->handles, msg->num_handles);
    if (hdr->txid != 0) {
      return UnbindInfo{UnbindInfo::kUnexpectedMessage, ZX_ERR_INVALID_ARGS};
    }
    return UnbindInfo{UnbindInfo::kPeerClosed, reinterpret_cast<fidl_epitaph_t*>(hdr)->error};
  }

  // If this is a response, look up the corresponding ResponseContext based on the txid.
  if (hdr->txid) {
    ResponseContext* context = nullptr;
    {
      std::scoped_lock lock(lock_);
      auto it = contexts_.find(hdr->txid);
      if (it != contexts_.end()) {
        context = &(*it);
        contexts_.erase(it);
        list_delete(static_cast<list_node_t*>(context));
      } else {
        fprintf(stderr, "%s: Received response for unknown txid %u.\n", __func__, hdr->txid);
        return UnbindInfo{UnbindInfo::kUnexpectedMessage, ZX_ERR_NOT_FOUND};
      }
    }
    const char* error_message = nullptr;
    // Perform in-place decoding
    fidl_trace(WillLLCPPDecode, context->type(), msg->bytes, msg->num_bytes, msg->num_handles);
    zx_status_t status = fidl_decode(context->type(), msg->bytes, msg->num_bytes, msg->handles,
                                     msg->num_handles, &error_message);
    fidl_trace(DidLLCPPDecode);
    if (status != ZX_OK) {
      context->OnError();
      return UnbindInfo{UnbindInfo::kDecodeError, status};
    }

    context->OnReply(reinterpret_cast<uint8_t*>(msg->bytes));
    return {};
  }

  // Dispatch events (received messages with no txid).
  return DispatchEvent(msg);
}

void ChannelRefTracker::Init(zx::channel channel) {
  std::scoped_lock lock(lock_);
  channel_weak_ = channel_ = std::make_shared<ChannelRef>(std::move(channel));
}

zx::channel ChannelRefTracker::WaitForChannel() {
  zx::channel channel;
  sync_completion_t on_delete;
  {
    std::scoped_lock lock(lock_);
    // Ensure that only one thread receives the channel.
    if (!channel_)
      return channel;
    channel.reset(channel_->ReleaseOnDelete(&on_delete));
    channel_ = nullptr;  // Allow the ChannelRef to be destroyed.
  }

  // Wait for all ChannelRefs to be released.
  auto status = sync_completion_wait(&on_delete, ZX_TIME_INFINITE);
  ZX_ASSERT_MSG(status == ZX_OK, "%s: Error waiting for channel to be released: %u.\n", __func__,
                status);
  return channel;
}

}  // namespace internal
}  // namespace fidl
