// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/client_base.h>
#include <lib/fidl/trace.h>
#include <lib/fidl/txn_header.h>
#include <lib/fit/function.h>
#include <stdio.h>

namespace fidl {
namespace internal {

// TODO(madhaviyengar): Move this constant to zircon/fidl.h
constexpr uint32_t kUserspaceTxidMask = 0x7FFFFFFF;

std::shared_ptr<ClientBase> ClientBase::Create(
    AnyTransport&& transport, async_dispatcher_t* dispatcher,
    AnyIncomingEventDispatcher&& event_dispatcher, AsyncEventHandler* error_handler,
    fidl::AnyTeardownObserver&& teardown_observer, ThreadingPolicy threading_policy,
    std::weak_ptr<ClientControlBlock> client_object_lifetime) {
  std::shared_ptr client_base = std::make_shared<ClientBase>();
  client_base->Bind(std::move(transport), dispatcher, std::move(event_dispatcher), error_handler,
                    std::move(teardown_observer), threading_policy,
                    std::move(client_object_lifetime));
  return client_base;
}

void ClientBase::Bind(AnyTransport&& transport, async_dispatcher_t* dispatcher,
                      AnyIncomingEventDispatcher&& event_dispatcher,
                      AsyncEventHandler* error_handler, AnyTeardownObserver&& teardown_observer,
                      ThreadingPolicy threading_policy,
                      std::weak_ptr<ClientControlBlock> client_object_lifetime) {
  ZX_DEBUG_ASSERT(!binding_.lock());
  auto binding = AsyncClientBinding::Create(
      dispatcher, std::make_shared<fidl::internal::AnyTransport>(std::move(transport)),
      shared_from_this(), error_handler, std::move(teardown_observer), threading_policy);
  binding_ = binding;
  client_object_lifetime_ = std::move(client_object_lifetime);
  dispatcher_ = dispatcher;
  event_dispatcher_ = std::move(event_dispatcher);
  binding->BeginFirstWait();
}

void ClientBase::AsyncTeardown() {
  if (auto binding = binding_.lock())
    binding->StartTeardown(std::move(binding));
}

void ClientBase::PrepareAsyncTxn(ResponseContext* context) {
  std::scoped_lock lock(lock_);

  // Generate the next txid. Verify that it doesn't overlap with any outstanding txids.
  do {
    do {
      context->txid_ = ++txid_base_ & kUserspaceTxidMask;  // txid must be within mask.
    } while (unlikely(!context->txid_));                   // txid must be non-zero.
  } while (unlikely(!contexts_.insert_or_find(context)));

  list_add_tail(&delete_list_, context);
}

void ClientBase::ForgetAsyncTxn(ResponseContext* context) {
  std::scoped_lock lock(lock_);

  ZX_ASSERT(context->InContainer());
  contexts_.erase(*context);
  list_delete(static_cast<list_node_t*>(context));
}

void ClientBase::ReleaseResponseContexts(fidl::UnbindInfo info) {
  // Release ownership on any outstanding |ResponseContext|s outside of locks.
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
    auto* context = static_cast<ResponseContext*>(node);
    // Depending on what kind of error caused teardown, we may want to propgate
    // the error to all other outstanding contexts.
    switch (info.reason()) {
      case fidl::Reason::kClose:
        // |kClose| is never used on the client side.
        __builtin_abort();
        break;
      case fidl::Reason::kUnbind:
        // The user explicitly initiated teardown.
      case fidl::Reason::kEncodeError:
      case fidl::Reason::kDecodeError:
        // These errors are specific to one call, whose corresponding context
        // would have been notified during |Dispatch| or making the call.
        context->OnError(fidl::Status::Unbound());
        break;
      case fidl::Reason::kPeerClosed:
      case fidl::Reason::kDispatcherError:
      case fidl::Reason::kTransportError:
      case fidl::Reason::kUnexpectedMessage:
        // These errors apply to all calls.
        context->OnError(info.ToError());
        break;
      default:
        // Should not reach here, but there is no compile-time approach to
        // guarantee it.
        ZX_PANIC("Unknown reason %d", static_cast<int>(info.reason()));
    }
  }
}

void ClientBase::SendTwoWay(fidl::OutgoingMessage& message, ResponseContext* context,
                            fidl::WriteOptions write_options) {
  if (auto transport = GetTransport()) {
    PrepareAsyncTxn(context);
    message.set_txid(context->Txid());
    message.Write(*transport, std::move(write_options));
    if (!message.ok()) {
      ForgetAsyncTxn(context);
      TryAsyncDeliverError(message.error(), context);
      HandleSendError(message.error());
    }
    return;
  }
  TryAsyncDeliverError(fidl::Status::Unbound(), context);
}

fidl::Status ClientBase::SendOneWay(::fidl::OutgoingMessage& message,
                                    fidl::WriteOptions write_options) {
  if (auto transport = GetTransport()) {
    message.set_txid(0);
    message.Write(*transport, std::move(write_options));
    if (!message.ok()) {
      HandleSendError(message.error());
      return message.error();
    }
    return fidl::Status::Ok();
  }
  return fidl::Status::Unbound();
}

void ClientBase::HandleSendError(fidl::Status error) {
  if (auto binding = binding_.lock()) {
    binding->HandleError(std::move(binding), {UnbindInfo{error}, ErrorOrigin::kSend});
  }
}

void ClientBase::TryAsyncDeliverError(::fidl::Status error, ResponseContext* context) {
  zx_status_t status = context->TryAsyncDeliverError(error, dispatcher_);
  if (status != ZX_OK) {
    context->OnError(error);
  }
}

std::optional<UnbindInfo> ClientBase::Dispatch(fidl::IncomingHeaderAndMessage& msg,
                                               internal::MessageStorageViewBase* storage_view) {
  if (fit::nullable epitaph = msg.maybe_epitaph(); unlikely(epitaph)) {
    return UnbindInfo::PeerClosed((*epitaph)->error);
  }

  auto* hdr = msg.header();
  if (hdr->txid == 0) {
    // Dispatch events (received messages with no txid).
    // Dispatch will always consume the message even if it is not recognized
    // (unknown interaction), so it is important to not reference the message or
    // header again after calling this.
    fidl::Status status = event_dispatcher_->DispatchEvent(msg, storage_view);
    if (status.ok()) {
      return std::nullopt;
    }
    return fidl::UnbindInfo{status};
  }

  // If this is a response, look up the corresponding ResponseContext based on the txid.
  ResponseContext* context = nullptr;
  {
    std::scoped_lock lock(lock_);
    context = contexts_.erase(hdr->txid);
    if (likely(context != nullptr)) {
      list_delete(static_cast<list_node_t*>(context));
    } else {
      // Received unknown txid.
      return UnbindInfo{
          Status::UnexpectedMessage(ZX_ERR_NOT_FOUND, fidl::internal::kErrorUnknownTxId)};
    }
  }
  return context->OnRawResult(std::move(msg), storage_view);
}

void ClientController::Bind(AnyTransport client_end, async_dispatcher_t* dispatcher,
                            AnyIncomingEventDispatcher&& event_dispatcher,
                            AsyncEventHandler* error_handler,
                            AnyTeardownObserver&& teardown_observer,
                            ThreadingPolicy threading_policy) {
  ZX_ASSERT(!client_impl_);
  // This three step dance is to setup a circular reference where |ClientBase|
  // weakly references |ClientControlBlock| and |ClientControlBlock| strongly
  // references |ClientBase|.
  control_ = std::make_shared<ClientControlBlock>(nullptr);
  client_impl_ = ClientBase::Create(std::move(client_end), dispatcher, std::move(event_dispatcher),
                                    error_handler, std::move(teardown_observer), threading_policy,
                                    control_->weak_from_this());
  // Actually fill in the |client_impl_|.
  *control_ = ClientControlBlock{client_impl_};
}

void ClientController::Unbind() {
  ZX_ASSERT(client_impl_);
  control_.reset();
  client_impl_->ClientBase::AsyncTeardown();
}

}  // namespace internal
}  // namespace fidl
