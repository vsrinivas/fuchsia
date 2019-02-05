// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_INTERNAL_PROXY_H_
#define LIB_FIDL_CPP_INTERNAL_PROXY_H_

#include <lib/fidl/cpp/message.h>
#include <zircon/types.h>

namespace fidl {
namespace internal {

// An interface for sending FIDL messages to a remote implementation.
class Proxy {
 public:
  virtual ~Proxy();

  // A new message has arrived.
  //
  // The memory backing the message will remain valid until this method returns,
  // at which point the memory might or might not be deallocated.
  virtual zx_status_t Dispatch_(Message message) = 0;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_INTERNAL_PROXY_H_
