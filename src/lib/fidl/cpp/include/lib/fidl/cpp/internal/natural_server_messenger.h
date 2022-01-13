// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_NATURAL_SERVER_MESSENGER_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_NATURAL_SERVER_MESSENGER_H_

#include <zircon/fidl.h>

namespace fidl {

class CompleterBase;
class HLCPPOutgoingMessage;

namespace internal {

// |NaturalServerMessenger| sends transactional messages with natural types.
//
// Objects of this class borrows a |CompleterBase| using a raw pointer. The
// |CompleterBase| instance must outlive its corresponding messenger.
class NaturalServerMessenger {
 public:
  explicit NaturalServerMessenger(fidl::CompleterBase* completer_base)
      : completer_base_(completer_base) {}

  // Sends a reply.
  //
  // Any send-time errors are notified to |completer_base|.
  //
  // |type| is used to validate the message.
  void SendReply(const fidl_type_t* type, HLCPPOutgoingMessage&& message) const;

 private:
  fidl::CompleterBase* completer_base_;
};

}  // namespace internal

}  // namespace fidl

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_INTERNAL_NATURAL_SERVER_MESSENGER_H_
