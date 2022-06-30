// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_UNKNOWN_INTERACTIONS_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_UNKNOWN_INTERACTIONS_H_

#include <lib/fidl/cpp/transaction_header.h>
#include <lib/fidl/cpp/wire/transaction.h>
#include <zircon/fidl.h>

namespace fidl {

// Identifies which kind of method an unknown interaction was.
enum class UnknownInteractionType {
  // Unknown interaction was for a one-way method.
  kOneWay,
  // Unknown interaction was for a two-way method.
  kTwoWay,
};

namespace internal {

// Returns the |UnknownInteractionType| of a message based on the |hdr|.
inline UnknownInteractionType UnknownInteractionTypeFromHeader(const fidl_message_header_t* hdr) {
  return hdr->txid == 0 ? UnknownInteractionType::kOneWay : UnknownInteractionType::kTwoWay;
}

// Openness of the protocol. Determines which unknown interactions can be
// handled.
enum class Openness {
  // Closed protocol. Unknown interactions cannot be handled.
  kClosed,
  // Ajar protocol. Only one-way unknown interactions can be handled.
  kAjar,
  // Open protocol. Both one-way and two-way unknown interactions can be
  // handled.
  kOpen,
};

inline bool CanHandleInteraction(Openness openness, UnknownInteractionType interaction_type) {
  return openness == Openness::kOpen ||
         (openness == Openness::kAjar && interaction_type == UnknownInteractionType::kOneWay);
}

// Represents the reply to a two-way unknown interaction. Used to build the
// |OutgoingMessage| to send the unknown interaction response to the client.
struct UnknownInteractionReply {
  fidl_message_header_t header;
  fidl_xunion_v2_t body;

  // Build an UnknownInteractionReply for the given |method_oridnal|. The
  // transaction ID is left as 0, and should be filled in by
  // |Transaction::Reply|.
  static UnknownInteractionReply MakeReplyFor(uint64_t method_ordinal,
                                              ::fidl::MessageDynamicFlags dynamic_flags);
};

// Builds and sends an unknown interaction reply with the given value for the
// Channel transport. This is used as part of the
// |UnknownInteractionHandlerEntry| for protocols which use the Channel
// transport. For protocols using Driver transport, see
// |SendDriverUnknownInteractionReply| in the fidl_driver library.
void SendChannelUnknownInteractionReply(UnknownInteractionReply reply, ::fidl::Transaction* txn);

}  // namespace internal

}  // namespace fidl
#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_UNKNOWN_INTERACTIONS_H_
