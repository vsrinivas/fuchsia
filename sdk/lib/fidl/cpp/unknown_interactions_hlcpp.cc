// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/unknown_interactions_hlcpp.h"

#include <lib/fidl/trace.h>

namespace fidl {
namespace internal {

static const fidl_xunion_tag_t kUnknownMethodTransportErrTag = 3;

static const size_t kResponseOffset = sizeof(fidl_message_header_t);

::fidl::HLCPPOutgoingMessage EncodeUnknownMethodResponse(::fidl::MessageEncoder* encoder) {
  fidl_trace(WillHLCPPEncode);

  encoder->Alloc(sizeof(fidl_xunion_v2_t));

  auto response_value = TransportErr::kUnknownMethod;
  if (::fidl::EncodingInlineSize<::fidl::TransportErr>(encoder) <=
      FIDL_ENVELOPE_INLINING_SIZE_THRESHOLD) {
    ::fidl::Encode(encoder, &response_value,
                   kResponseOffset + offsetof(fidl_xunion_v2_t, envelope));

    fidl_xunion_v2_t* xunion = encoder->GetPtr<fidl_xunion_v2_t>(kResponseOffset);
    xunion->tag = kUnknownMethodTransportErrTag;
    xunion->envelope.num_handles = 0;
    xunion->envelope.flags = FIDL_ENVELOPE_FLAGS_INLINING_MASK;
  } else {
    const size_t length_before = encoder->CurrentLength();
    ::fidl::Encode(
        encoder, &response_value,
        encoder->Alloc(::fidl::EncodingInlineSize<::fidl::TransportErr, ::fidl::Encoder>(encoder)));

    fidl_xunion_v2_t* xunion = encoder->GetPtr<fidl_xunion_v2_t>(kResponseOffset);
    xunion->tag = kUnknownMethodTransportErrTag;
    xunion->envelope.num_bytes = static_cast<uint32_t>(encoder->CurrentLength() - length_before);
    xunion->envelope.num_handles = 0;
    xunion->envelope.flags = 0;
  }

  fidl_trace(DidHLCPPEncode, &kFidlInternalUnknownMethodResponseTable,
             encoder->GetPtr<const char>(0), encoder->CurrentLength(),
             encoder->CurrentHandleCount());

  return encoder->GetMessage();
}

}  // namespace internal
}  // namespace fidl
