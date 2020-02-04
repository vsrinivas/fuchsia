// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_PORT_H
#define PLATFORM_PORT_H

#include <memory>

#include "magma_util/status.h"

namespace magma {

class PlatformPort {
 public:
  static std::unique_ptr<PlatformPort> Create();

  virtual ~PlatformPort() {}

  // Closes the port. This will cause any thread blocked in Wait to return an error.
  virtual void Close() = 0;

  // Waits for a port to return a packet.
  // If a packet is available before the time timeout expires, |key_out| will be set.
  virtual Status Wait(uint64_t* key_out, uint64_t timeout_ms) = 0;

  Status Wait(uint64_t* key_out) { return Wait(key_out, UINT64_MAX); }
};

}  // namespace magma

#endif  // PLATFORM_PORT_H
