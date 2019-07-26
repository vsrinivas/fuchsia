// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDKTL_PROTOCOL_EMPTY_PROTOCOL_H_
#define DDKTL_PROTOCOL_EMPTY_PROTOCOL_H_

#include <ddk/driver.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>

// Mixin for protocol which have no protocol ops.

namespace ddk {

template <uint32_t PROTO_ID>
class EmptyProtocol : public internal::base_protocol {
 public:
  EmptyProtocol() {
    // Can only inherit from one base_protocol implementation
    ZX_ASSERT(this->ddk_proto_id_ == 0);
    ddk_proto_id_ = PROTO_ID;
  }
};

}  // namespace ddk

#endif  // DDKTL_PROTOCOL_EMPTY_PROTOCOL_H_
