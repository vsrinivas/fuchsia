// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/transaction_header.h>
#include <lib/fidl/cpp/wire/internal/transport_channel.h>
#include <lib/fidl/cpp/wire/message.h>
#include <lib/fidl/cpp/wire/unknown_interactions.h>

#include <cstring>

namespace fidl::internal {

UnknownMethodReply UnknownMethodReply::MakeReplyFor(uint64_t method_ordinal,
                                                    ::fidl::MessageDynamicFlags dynamic_flags) {
  const fidl_union_tag_t kTransportErrTag = 3;
  const zx_status_t kUnknownMethodStatus = ZX_ERR_NOT_SUPPORTED;
  UnknownMethodReply reply{
      .body{
          .tag = kTransportErrTag,
          .envelope =
              {
                  .num_handles = 0,
                  .flags = FIDL_ENVELOPE_FLAGS_INLINING_MASK,
              },
      },
  };
  InitTxnHeader(&reply.header, 0, method_ordinal, dynamic_flags);

  static_assert(sizeof(reply.body.envelope.inline_value) == sizeof(kUnknownMethodStatus));
  ::std::memcpy(reply.body.envelope.inline_value, &kUnknownMethodStatus,
                sizeof(kUnknownMethodStatus));

  return reply;
}

void SendChannelUnknownMethodReply(UnknownMethodReply reply, ::fidl::Transaction *txn) {
  auto message = ::fidl::OutgoingMessage::Create_InternalMayBreak(
      ::fidl::OutgoingMessage::InternalByteBackedConstructorArgs{
          .transport_vtable = &ChannelTransport::VTable,
          .bytes = reinterpret_cast<uint8_t *>(&reply),
          .num_bytes = sizeof(reply),
          .handles = nullptr,
          .handle_metadata = nullptr,
          .num_handles = 0,
          .is_transactional = true,
      });
  txn->Reply(&message, {});
}

}  // namespace fidl::internal
