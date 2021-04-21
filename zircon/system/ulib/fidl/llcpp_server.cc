// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/server.h>

namespace fidl {

namespace internal {

::fidl::DispatchResult TryDispatch(void* impl, fidl_incoming_msg_t* msg, ::fidl::Transaction* txn,
                                   const MethodEntry* begin, const MethodEntry* end) {
  fidl_message_header_t* hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
  while (begin < end) {
    if (hdr->ordinal == begin->ordinal) {
      zx_status_t decode_status = begin->dispatch(impl, msg, txn);
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
