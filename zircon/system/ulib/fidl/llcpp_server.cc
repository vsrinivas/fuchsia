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
    txn->InternalError({::fidl::UnbindInfo::kUnexpectedMessage, msg.status()});
    return ::fidl::DispatchResult::kNotFound;
  }
  auto* hdr = msg.header();
  while (begin < end) {
    if (hdr->ordinal == begin->ordinal) {
      zx_status_t decode_status = begin->dispatch(impl, std::move(msg), txn);
      if (unlikely(decode_status != ZX_OK)) {
        txn->InternalError({::fidl::UnbindInfo::kDecodeError, decode_status});
      }
      return ::fidl::DispatchResult::kFound;
    }
    ++begin;
  }
  return ::fidl::DispatchResult::kNotFound;
}

}  // namespace internal

}  // namespace fidl
