// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_EVENT_H_
#define SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_EVENT_H_

#include <memory>

#include "magma_util/status.h"

namespace magma {

// PlatformEvent is a one-shot event: initial state is unsignaled;
// after signaling, state is forever signaled.
class PlatformEvent {
 public:
  static std::unique_ptr<PlatformEvent> Create();

  virtual ~PlatformEvent() {}

  virtual void Signal() = 0;

  // Returns MAGMA_STATUS_OK if the event is signaled before the
  // timeout expires.
  virtual magma::Status Wait(uint64_t timeout_ms) = 0;

  magma::Status Wait() { return Wait(UINT64_MAX); }
};

}  // namespace magma

#endif  // SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_PLATFORM_EVENT_H_
