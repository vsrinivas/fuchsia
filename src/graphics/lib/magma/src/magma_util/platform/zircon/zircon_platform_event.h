// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_ZIRCON_ZIRCON_PLATFORM_EVENT_H_
#define SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_ZIRCON_ZIRCON_PLATFORM_EVENT_H_

#include <lib/zx/event.h>
#include <lib/zx/time.h>

#include "magma_util/macros.h"
#include "platform_event.h"

namespace magma {

class ZirconPlatformEvent : public PlatformEvent {
 public:
  ZirconPlatformEvent(zx::event event) : zx_event_(std::move(event)) {}

  void Signal() override {
    zx_status_t status = zx_event_.signal(0u, zx_signal());
    DASSERT(status == ZX_OK);
  }

  magma::Status Wait(uint64_t timeout_ms) override {
    zx_status_t status = zx_event_.wait_one(
        zx_signal(), zx::deadline_after(zx::duration(magma::ms_to_signed_ns(timeout_ms))), nullptr);
    switch (status) {
      case ZX_OK:
        return MAGMA_STATUS_OK;
      case ZX_ERR_TIMED_OUT:
        return MAGMA_STATUS_TIMED_OUT;
      case ZX_ERR_CANCELED:
        return MAGMA_STATUS_CONNECTION_LOST;
      default:
        return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Unexpected wait() status: %d.", status);
    }
  }

  zx_handle_t zx_handle() const { return zx_event_.get(); }

  zx_signals_t zx_signal() const { return ZX_EVENT_SIGNALED; }

 private:
  zx::event zx_event_;
};

}  // namespace magma

#endif  // SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_ZIRCON_ZIRCON_PLATFORM_EVENT_H_
