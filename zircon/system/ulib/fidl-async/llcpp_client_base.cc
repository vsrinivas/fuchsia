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
}

void ClientBase::Unbind() {
  if (auto binding = binding_.lock())
    binding->Unbind(std::move(binding));
}

ClientBase::ClientBase(zx::channel channel, async_dispatcher_t* dispatcher,
                       TypeErasedOnUnboundFn on_unbound) {
  list_initialize(&contexts_.node);
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
      context->txid = ++contexts_.txid & kUserspaceTxidMask;  // txid must be within mask.
    } while (!context->txid);  // txid must be non-zero.
    ResponseContext* entry = nullptr;
    list_for_every_entry(&contexts_.node, entry, ResponseContext, node) {
      if (entry->txid == context->txid) {
        found = true;
        break;
      }
    }
  } while (found);

  // Insert the ResponseContext.
  list_add_tail(&contexts_.node, &context->node);
}

void ClientBase::ForgetAsyncTxn(ResponseContext* context) {
  std::scoped_lock lock(lock_);
  ZX_ASSERT(list_in_list(&context->node));
  list_delete(&context->node);
}

std::shared_ptr<AsyncBinding> ClientBase::GetBinding() {
  return binding_.lock();
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
      ResponseContext* entry = nullptr;
      list_for_every_entry(&contexts_.node, entry, ResponseContext, node) {
        if (entry->txid == hdr->txid) {
          context = entry;
          list_delete(&entry->node);  // This is safe since we break immediately after.
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
  Dispatch(msg, context);
}

}  // namespace internal
}  // namespace fidl
