// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/transaction_header.h>
#include <lib/fidl/cpp/wire/server.h>

namespace fidl {

namespace internal {

const UnknownInteractionHandlerEntry UnknownInteractionHandlerEntry::kClosedProtocolHandlerEntry{
    .openness = ::fidl::internal::Openness::kClosed,
    .dispatch = nullptr,
    .send_reply = nullptr,
};

::fidl::DispatchResult TryDispatch(void* impl, ::fidl::IncomingHeaderAndMessage& msg,
                                   fidl::internal::MessageStorageViewBase* storage_view,
                                   ::fidl::Transaction* txn, const MethodEntry* begin,
                                   const MethodEntry* end) {
  if (!msg.ok()) {
    txn->InternalError(fidl::UnbindInfo{msg}, fidl::ErrorOrigin::kReceive);
    // |TryDispatch| is used to ad-hoc compose protocols by trying a series of
    // |TryDispatch|. If the message has an error, exit the cascade of dispatch
    // attempts early since it is meaningless to keep trying otherwise.
    return ::fidl::DispatchResult::kFound;
  }
  auto* hdr = msg.header();
  while (begin < end) {
    if (hdr->ordinal == begin->ordinal) {
      fidl::Status decode_status = begin->dispatch(impl, std::move(msg), storage_view, txn);
      if (unlikely(!decode_status.ok())) {
        ZX_DEBUG_ASSERT(decode_status.reason() == fidl::Reason::kDecodeError);
        txn->InternalError(UnbindInfo{decode_status}, fidl::ErrorOrigin::kReceive);
      }
      return ::fidl::DispatchResult::kFound;
    }
    ++begin;
  }
  return ::fidl::DispatchResult::kNotFound;
}

void Dispatch(void* impl, ::fidl::IncomingHeaderAndMessage& msg,
              fidl::internal::MessageStorageViewBase* storage_view, ::fidl::Transaction* txn,
              const MethodEntry* begin, const MethodEntry* end,
              const UnknownInteractionHandlerEntry* unknown_interaction_handler) {
  ::fidl::DispatchResult result = TryDispatch(impl, msg, storage_view, txn, begin, end);
  switch (result) {
    case ::fidl::DispatchResult::kNotFound: {
      auto* hdr = msg.header();
      ::fidl::UnknownInteractionType unknown_interaction_type =
          ::fidl::internal::UnknownInteractionTypeFromHeader(hdr);
      if (::fidl::IsFlexibleInteraction(hdr) &&
          ::fidl::internal::CanHandleMethod(unknown_interaction_handler->openness,
                                            unknown_interaction_type)) {
        if (unknown_interaction_type == ::fidl::UnknownInteractionType::kTwoWay) {
          auto reply = ::fidl::internal::UnknownInteractionReply::MakeReplyFor(
              hdr->ordinal, ::fidl::MessageDynamicFlags::kFlexibleMethod);
          (unknown_interaction_handler->send_reply)(reply, txn);
        }
        std::move(msg).CloseHandles();
        unknown_interaction_handler->dispatch(impl, hdr->ordinal, unknown_interaction_type, txn);
        break;
      }
      std::move(msg).CloseHandles();
      txn->InternalError(::fidl::UnbindInfo::UnknownOrdinal(), ::fidl::ErrorOrigin::kReceive);
    } break;
    case ::fidl::DispatchResult::kFound:
      break;
  }
}

::fidl::Status WeakEventSenderInner::SendEvent(::fidl::OutgoingMessage& message) const {
  if (auto binding = binding_.lock()) {
    message.set_txid(0);
    message.Write(binding->transport());
    if (!message.ok()) {
      HandleSendError(message.error());
      return message.error();
    }
    return fidl::Status::Ok();
  }
  return fidl::Status::Unbound();
}

void WeakEventSenderInner::HandleSendError(fidl::Status error) const {
  if (auto binding = binding_.lock()) {
    binding->HandleError(std::move(binding), {UnbindInfo{error}, ErrorOrigin::kSend});
  }
}

}  // namespace internal

}  // namespace fidl
