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

ClientBase::~ClientBase() {
  Unbind();
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

void ClientBase::Unbind() {
  if (auto binding = binding_.lock())
    binding->Unbind(std::move(binding));
}

ClientBase::ClientBase(zx::channel channel, async_dispatcher_t* dispatcher,
                       TypeErasedOnUnboundFn on_unbound)
    : binding_(AsyncBinding::CreateClientBinding(
          dispatcher, std::move(channel), this,
          [this](std::shared_ptr<AsyncBinding>&, fidl_msg_t* msg, bool*) { return Dispatch(msg); },
          std::move(on_unbound))) {}

zx_status_t ClientBase::Bind() {
  if (auto binding = binding_.lock())
    return binding->BeginWait();
  return ZX_ERR_CANCELED;
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

std::optional<UnbindInfo> ClientBase::Dispatch(fidl_msg_t* msg) {
  auto* hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);

  if (hdr->ordinal == kFidlOrdinalEpitaph) {
    zx_handle_close_many(msg->handles, msg->num_handles);
    if (hdr->txid != 0) {
      return ::fidl::UnbindInfo{::fidl::UnbindInfo::kUnexpectedMessage, ZX_ERR_INVALID_ARGS};
    }
    return ::fidl::UnbindInfo{::fidl::UnbindInfo::kPeerClosed,
                              reinterpret_cast<fidl_epitaph_t*>(hdr)->error};
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
      return ::fidl::UnbindInfo{::fidl::UnbindInfo::kDecodeError, status};
    }
    context->OnReply(reinterpret_cast<uint8_t*>(msg->bytes));
    return {};
  }

  // Dispatch events (received messages with no txid).
  return DispatchEvent(msg);
}

}  // namespace internal
}  // namespace fidl
