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
enum class UnknownMethodType {
  // Unknown method was a one-way method.
  kOneWay,
  // Unknown method was a two-way method.
  kTwoWay,
};

namespace internal {

// Returns the |UnknownMethodType| of a message based on the |hdr|.
inline UnknownMethodType UnknownMethodTypeFromHeader(const fidl_message_header_t* hdr) {
  return hdr->txid == 0 ? UnknownMethodType::kOneWay : UnknownMethodType::kTwoWay;
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

// Returns true if a protocol with the given |Openness| can handle a client-sent
// unknown method with the given |UnknownMethodType|.
inline bool CanHandleMethod(Openness openness, UnknownMethodType interaction_type) {
  return openness == Openness::kOpen ||
         (openness == Openness::kAjar && interaction_type == UnknownMethodType::kOneWay);
}

// Returns true if a protocol with the given |Openness| can handle a server-sent
// unknown event with the given |UnknownMethodType|.
//
// Note: currently only one-way server-sent messages are defined, so this always
// returns false if |UnknownMethodType| is |kTwoWay|. The argument is
// included to simplify the generated event handler.
inline bool CanHandleEvent(Openness openness, UnknownMethodType interaction_type) {
  return interaction_type == UnknownMethodType::kOneWay &&
         (openness == Openness::kOpen || openness == Openness::kAjar);
}

// Represents the reply to a two-way unknown interaction. Used to build the
// |OutgoingMessage| to send the unknown interaction response to the client.
struct UnknownMethodReply {
  fidl_message_header_t header;
  fidl_xunion_v2_t body;

  // Build an UnknownMethodReply for the given |method_oridnal|. The
  // transaction ID is left as 0, and should be filled in by
  // |Transaction::Reply|.
  static UnknownMethodReply MakeReplyFor(uint64_t method_ordinal,
                                         ::fidl::MessageDynamicFlags dynamic_flags);
};

// Builds and sends an unknown interaction reply with the given value for the
// Channel transport. This is used as part of the
// |UnknownMethodHandlerEntry| for protocols which use the Channel
// transport. For protocols using Driver transport, see
// |SendDriverUnknownMethodReply| in the fidl_driver library.
void SendChannelUnknownMethodReply(UnknownMethodReply reply, ::fidl::Transaction* txn);

}  // namespace internal

}  // namespace fidl
#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_UNKNOWN_INTERACTIONS_H_
