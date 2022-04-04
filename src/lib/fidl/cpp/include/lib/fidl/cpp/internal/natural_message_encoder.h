// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_NATURAL_MESSAGE_ENCODER_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_NATURAL_MESSAGE_ENCODER_H_

#include <lib/fidl/cpp/natural_encoder.h>
#include <lib/fidl/cpp/natural_types.h>
#include <lib/fidl/internal.h>
#include <lib/fidl/llcpp/message.h>
#include <zircon/fidl.h>

namespace fidl::internal {

// The NaturalMessageEncoder produces an |OutgoingMessage|, representing a transactional
// message.
class NaturalMessageEncoder final {
 public:
  NaturalMessageEncoder(const TransportVTable* vtable, uint64_t ordinal);

  NaturalMessageEncoder(NaturalMessageEncoder&&) noexcept = default;
  NaturalMessageEncoder& operator=(NaturalMessageEncoder&&) noexcept = default;

  ~NaturalMessageEncoder() = default;

  NaturalBodyEncoder& body_encoder() { return body_encoder_; }

  // Encode |payload| as part of a request/response message.
  //
  // To reducing branching in generated code, |payload| may be |std::nullopt|, in
  // which case the message will be encoded without a payload (header-only
  // messages).
  //
  // Return an outgoing message representing the encoded header plus body.
  // Handle ownership will be transferred to the outgoing message.
  // Do not encode another value until the message is sent.
  template <typename Payload = const cpp17::nullopt_t&>
  fidl::OutgoingMessage EncodeTransactionalMessage(Payload&& payload = cpp17::nullopt) {
    // When the caller omits the |payload| argument, it will default to
    // |cpp17::nullopt|, which is of type |cpp17::nullopt_t|.
    constexpr bool kHasPayload = !std::is_same_v<cpp20::remove_cvref_t<Payload>, cpp17::nullopt_t>;
    if constexpr (kHasPayload) {
      body_encoder().Alloc(
          NaturalEncodingInlineSize<Payload, NaturalCodingConstraintEmpty>(&body_encoder()));
      NaturalCodingTraits<Payload, NaturalCodingConstraintEmpty>::Encode(
          &body_encoder(), &payload, sizeof(fidl_message_header_t), kRecursionDepthInitial);
      return GetMessage();
    } else {
      return GetMessage();
    }
  }

  void Reset(uint64_t ordinal);

 private:
  fidl::OutgoingMessage GetMessage();

  NaturalBodyEncoder body_encoder_;

  void EncodeMessageHeader(uint64_t ordinal);
};

}  // namespace fidl::internal

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_NATURAL_MESSAGE_ENCODER_H_
