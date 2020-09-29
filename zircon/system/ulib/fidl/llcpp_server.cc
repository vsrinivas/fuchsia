// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/server.h>

namespace fidl {

namespace internal {

::fidl::DispatchResult TryDispatch(void* impl, fidl_msg_t* msg, ::fidl::Transaction* txn,
                                   MethodEntry* begin, MethodEntry* end) {
  fidl_message_header_t* hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
  while (begin < end) {
    if (hdr->ordinal == begin->ordinal) {
      const char* error_message;
      zx_status_t status = fidl_decode(begin->type, msg->bytes, msg->num_bytes, msg->handles,
                                       msg->num_handles, &error_message);
      if (status != ZX_OK) {
        txn->InternalError({::fidl::UnbindInfo::kDecodeError, status});
      } else {
        begin->dispatch(impl, msg->bytes, txn);
      }
      return ::fidl::DispatchResult::kFound;
    }
    ++begin;
  }
  return ::fidl::DispatchResult::kNotFound;
}

}  // namespace internal

}  // namespace fidl
