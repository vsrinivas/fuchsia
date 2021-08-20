// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_NATURAL_CLIENT_MESSENGER_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_NATURAL_CLIENT_MESSENGER_H_

#include <zircon/fidl.h>

namespace fidl {

class Result;
class HLCPPOutgoingMessage;

namespace internal {

class ClientBase;
class ResponseContext;

// |NaturalClientMessenger| sends transactional messages with natural types.
//
// Objects of this class borrows a |ClientBase| using a raw pointer. The
// |ClientBase| instance must outlive its corresponding messenger.
//
// For two-way calls, the messenger registers a transaction ID with
// |ClientBase|.
class NaturalClientMessenger {
 public:
  explicit NaturalClientMessenger(fidl::internal::ClientBase* client_base)
      : client_base_(client_base) {}

  // Sends a two way message.
  //
  // |type| is used to validate the message.
  //
  // If error happens during sending, notifies |context| of the error.
  //
  // Otherwise, |context| ownership is passed to |ClientBase|.
  void TwoWay(const fidl_type_t* type, HLCPPOutgoingMessage&& message,
              fidl::internal::ResponseContext* context) const;

  // Sends a one way message.
  //
  // Any send-time errors are propagated via the return value.
  //
  // |type| is used to validate the message.
  fidl::Result OneWay(const fidl_type_t* type, HLCPPOutgoingMessage&& message) const;

 private:
  // The client messaging implementation.
  fidl::internal::ClientBase* client_base_;
};

}  // namespace internal
}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_NATURAL_CLIENT_MESSENGER_H_
