// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_NO_INTERFACE_H_
#define LIB_FIDL_CPP_BINDINGS_NO_INTERFACE_H_

#include "lib/fidl/cpp/bindings/message.h"
#include "lib/fidl/cpp/bindings/message_validator.h"

namespace fidl {

// NoInterface is for use in cases when a non-existent or empty interface is
// needed.

class NoInterfaceProxy;
class NoInterfaceStub;

class NoInterface {
 public:
  static const char* Name_;
  using Proxy_ = NoInterfaceProxy;
  using Stub_ = NoInterfaceStub;
  using RequestValidator_ = internal::PassThroughValidator;
  using ResponseValidator_ = internal::PassThroughValidator;
  virtual ~NoInterface() {}
};

class NoInterfaceProxy : public NoInterface {
 public:
  explicit NoInterfaceProxy(MessageReceiver* receiver) {}
};

class NoInterfaceStub : public MessageReceiverWithResponder {
 public:
  NoInterfaceStub() {}
  void set_sink(NoInterface* sink) {}
  NoInterface* sink() { return nullptr; }
  bool Accept(Message* message) override;
  bool AcceptWithResponder(Message* message,
                           MessageReceiver* responder) override;
};

// AnyInterface is for use in cases where any interface would do (e.g., see the
// Shell::Connect method).

typedef NoInterface AnyInterface;

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_NO_INTERFACE_H_
