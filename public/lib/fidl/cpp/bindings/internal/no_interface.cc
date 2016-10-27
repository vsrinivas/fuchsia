// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/no_interface.h"

namespace fidl {

const char* NoInterface::Name_ = "fidl::NoInterface";

bool NoInterfaceStub::Accept(Message* message) {
  return false;
}

bool NoInterfaceStub::AcceptWithResponder(Message* message,
                                          MessageReceiver* responder) {
  return false;
}

}  // namespace fidl
