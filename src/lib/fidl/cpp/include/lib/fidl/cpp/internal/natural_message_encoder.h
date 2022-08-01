// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_NATURAL_MESSAGE_ENCODER_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_NATURAL_MESSAGE_ENCODER_H_

#include <lib/fidl/cpp/natural_encoder.h>
#include <lib/fidl/cpp/natural_types.h>
#include <lib/fidl/cpp/transaction_header.h>
#include <lib/fidl/cpp/wire/message.h>
#include <lib/fidl/internal.h>
#include <zircon/fidl.h>

namespace fidl::internal {

// The NaturalMessageEncoder produces an |OutgoingMessage|, representing a transactional
// message.
class NaturalMessageEncoder final {
 public:
  NaturalMessageEncoder(const TransportVTable* vtable, uint64_t ordinal,
                        MessageDynamicFlags dynamic_flags);

  ~NaturalMessageEncoder() = default;

  NaturalBodyEncoder& body_encoder() { return body_encoder_; }

  // Encode |payload| as the body of a request/response message.
  //
  // This method is not necessary if the request/response does not have a body.
  //
  // |GetMessage| is used to extract the encoded message.
  // Do not encode another value until the message is sent.
  // Do not move the encoder object until the message is sent.
  template <typename Payload>
  void EncodeBody(Payload&& payload) {
    body_encoder().Alloc(
        NaturalEncodingInlineSize<Payload, NaturalCodingConstraintEmpty>(&body_encoder()));
    NaturalCodingTraits<Payload, NaturalCodingConstraintEmpty>::Encode(
        &body_encoder(), &payload, sizeof(fidl_message_header_t), kRecursionDepthInitial);
  }

  void Reset(uint64_t ordinal, MessageDynamicFlags dynamic_flags);

  // Return an outgoing message representing the encoded header plus body.
  // Handle ownership will be transferred to the outgoing message.
  fidl::OutgoingMessage GetMessage();

 private:
  NaturalBodyEncoder body_encoder_;

  void EncodeMessageHeader(uint64_t ordinal, MessageDynamicFlags dynamic_flags);
};

}  // namespace fidl::internal

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_NATURAL_MESSAGE_ENCODER_H_
