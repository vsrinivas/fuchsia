// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS2_INTERNAL_MESSAGE_HANDLER_H_
#define LIB_FIDL_CPP_BINDINGS2_INTERNAL_MESSAGE_HANDLER_H_

#include <fidl/cpp/message.h>
#include <zircon/types.h>

namespace fidl {
namespace internal {

class MessageHandler {
 public:
  virtual zx_status_t OnMessage(Message message) = 0;

 protected:
  virtual ~MessageHandler();
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS2_INTERNAL_MESSAGE_HANDLER_H_
