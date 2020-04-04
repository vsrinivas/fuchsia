// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <lib/fidl/txn_header.h>
#include <lib/fidl-async/cpp/client_base.h>
#include <lib/fit/function.h>

namespace fidl {
namespace internal {

// TODO(madhaviyengar): Move this constant to zircon/fidl.h
constexpr uint32_t kUserspaceTxidMask = 0x7FFFFFFF;

ClientBase::~ClientBase() {
  Unbind();
  // Release any managed ResponseContexts.
  list_node_t delete_list = LIST_INITIAL_CLEARED_VALUE;
  {
    std::scoped_lock lock(lock_);
    list_move(&contexts_, &delete_list);
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
                       TypeErasedOnUnboundFn on_unbound) {
  binding_ = AsyncBinding::CreateClientBinding(
      dispatcher, std::move(channel), this, fit::bind_member(this, &ClientBase::InternalDispatch),
      std::move(on_unbound));
}

zx_status_t ClientBase::Bind() {
  if (auto binding = binding_.lock())
    return binding->BeginWait();
  return ZX_ERR_CANCELED;
}

void ClientBase::PrepareAsyncTxn(ResponseContext* context) {
  std::scoped_lock lock(lock_);

  // Generate the next txid. Verify that it doesn't overlap with any outstanding txids.
  bool found;
  do {
    found = false;
    do {
      context->txid_ = ++txid_base_ & kUserspaceTxidMask;  // txid must be within mask.
    } while (!context->txid_);  // txid must be non-zero.
    list_node_t* node = nullptr;
    list_for_every(&contexts_, node) {
      if (static_cast<ResponseContext*>(node)->txid_ == context->txid_) {
        found = true;
        break;
      }
    }
  } while (found);

  // Insert the ResponseContext.
  list_add_tail(&contexts_, static_cast<list_node_t*>(context));
}

void ClientBase::ForgetAsyncTxn(ResponseContext* context) {
  auto* node = static_cast<list_node_t*>(context);
  std::scoped_lock lock(lock_);
  ZX_ASSERT(list_in_list(node));
  list_delete(node);
}

void ClientBase::InternalDispatch(std::shared_ptr<AsyncBinding>&, fidl_msg_t* msg, bool*,
                                  zx_status_t* status) {
  auto* hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
  // Check the message header. If invalid, return and trigger unbinding.
  if ((*status = fidl_validate_txn_header(hdr)) != ZX_OK) {
    fprintf(stderr, "%s: Received message with invalid header.\n", __func__);
    return;
  }

  // If this is a response, look up the corresponding ResponseContext based on the txid.
  ResponseContext* context = nullptr;
  if (hdr->txid) {
    {
      std::scoped_lock lock(lock_);
      list_node_t* node = nullptr;
      list_for_every(&contexts_, node) {
        auto* entry = static_cast<ResponseContext*>(node);
        if (entry->txid_ == hdr->txid) {
          context = entry;
          list_delete(node);  // This is safe since we break immediately after.
          break;
        }
      }
    }

    // If there was no associated context, log the unknown txid and exit.
    if (!context) {
      fprintf(stderr, "%s: Received response for unknown txid %u.\n", __func__, hdr->txid);
      *status = ZX_ERR_NOT_FOUND;
      return;
    }
  }

  // Dispatch the message
  *status = Dispatch(msg, context);
}

}  // namespace internal
}  // namespace fidl
