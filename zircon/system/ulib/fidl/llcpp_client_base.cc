// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <lib/fidl/txn_header.h>
#include <lib/fidl/llcpp/client_base.h>
#include <lib/fit/function.h>

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

void ClientBase::Close(zx_status_t epitaph) {
  if (auto binding = binding_.lock())
    binding->Close(std::move(binding), epitaph);
}

ClientBase::ClientBase(zx::channel channel, async_dispatcher_t* dispatcher,
                       TypeErasedOnUnboundFn on_unbound)
    : binding_(AsyncBinding::CreateClientBinding(
          dispatcher, std::move(channel), this,
          [this](std::shared_ptr<AsyncBinding>&, fidl_msg_t* msg, bool*, zx_status_t* status) {
            *status = Dispatch(msg);
          },
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
    } while (!context->txid_);  // txid must be non-zero.
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

zx_status_t ClientBase::Dispatch(fidl_msg_t* msg) {
  auto* hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);

  // Check the message header. If invalid, return and trigger unbinding.
  zx_status_t status = fidl_validate_txn_header(hdr);
  if (status != ZX_OK) {
    fprintf(stderr, "%s: Received message with invalid header.\n", __func__);
    return status;
  }

  // If this is a response, look up the corresponding ResponseContext based on the txid.
  ResponseContext* context = nullptr;
  if (hdr->txid) {
    {
      std::scoped_lock lock(lock_);
      auto it = contexts_.find(hdr->txid);
      if (it != contexts_.end()) {
        context = &(*it);
        contexts_.erase(it);
        list_delete(static_cast<list_node_t*>(context));
      }
    }

    // If there was no associated context, log the unknown txid and exit.
    if (!context) {
      fprintf(stderr, "%s: Received response for unknown txid %u.\n", __func__, hdr->txid);
      return ZX_ERR_NOT_FOUND;
    }
  }

  // Dispatch the message
  return Dispatch(msg, context);
}

}  // namespace internal
}  // namespace fidl
