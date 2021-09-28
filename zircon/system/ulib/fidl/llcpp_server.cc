// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/server.h>

namespace fidl {

namespace internal {

::fidl::DispatchResult TryDispatch(void* impl, ::fidl::IncomingMessage& msg,
                                   ::fidl::Transaction* txn, const MethodEntry* begin,
                                   const MethodEntry* end) {
  if (!msg.ok()) {
    txn->InternalError(fidl::UnbindInfo{msg});
    return ::fidl::DispatchResult::kNotFound;
  }
  auto* hdr = msg.header();
  while (begin < end) {
    if (hdr->ordinal == begin->ordinal) {
      zx_status_t decode_status = begin->dispatch(impl, std::move(msg), txn);
      if (unlikely(decode_status != ZX_OK)) {
        txn->InternalError(UnbindInfo{fidl::Result::DecodeError(decode_status)});
      }
      return ::fidl::DispatchResult::kFound;
    }
    ++begin;
  }
  return ::fidl::DispatchResult::kNotFound;
}

::fidl::Result WeakEventSenderInner::SendEvent(::fidl::OutgoingMessage& message) const {
  if (auto binding = binding_.lock()) {
    message.set_txid(0);
    message.Write(binding->channel());
    if (!message.ok()) {
      return message.error();
    }
    return fidl::Result::Ok();
  }
  return fidl::Result::Unbound();
}

}  // namespace internal

}  // namespace fidl
