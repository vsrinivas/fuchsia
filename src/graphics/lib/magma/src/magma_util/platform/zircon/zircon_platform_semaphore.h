// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_ZIRCON_ZIRCON_PLATFORM_SEMAPHORE_H_
#define SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_ZIRCON_ZIRCON_PLATFORM_SEMAPHORE_H_

#include <lib/zx/event.h>

#include "magma_util/macros.h"
#include "platform_semaphore.h"
#include "platform_trace.h"

namespace magma {

class ZirconPlatformSemaphore : public PlatformSemaphore {
 public:
  ZirconPlatformSemaphore(zx::event event, uint64_t koid) : event_(std::move(event)), koid_(koid) {}

  void set_local_id(uint64_t id) override {
    DASSERT(id);
    DASSERT(!local_id_);
    local_id_ = id;
  }

  uint64_t koid() const { return koid_; }

  uint64_t id() const override { return local_id_ ? local_id_ : koid_; }
  uint64_t global_id() const override { return koid_; }

  bool duplicate_handle(uint32_t* handle_out) const override;

  void Reset() override {
    event_.signal(zx_signal(), 0);
    TRACE_DURATION("magma:sync", "semaphore reset", "id", koid_);
    TRACE_FLOW_END("magma:sync", "semaphore signal", koid_);
    TRACE_FLOW_END("magma:sync", "semaphore wait async", koid_);
  }

  void Signal() override {
    TRACE_FLOW_BEGIN("gfx", "event_signal", koid_);
    TRACE_DURATION("magma:sync", "semaphore signal", "id", koid_);
    TRACE_FLOW_BEGIN("magma:sync", "semaphore signal", koid_);
    zx_status_t status = event_.signal(0u, zx_signal());
    DASSERT(status == ZX_OK);
  }

  magma::Status WaitNoReset(uint64_t timeout_ms) override;
  magma::Status Wait(uint64_t timeout_ms) override;

  bool WaitAsync(PlatformPort* port, uint64_t key) override;

  zx_handle_t zx_handle() const { return event_.get(); }

  zx_signals_t zx_signal() const { return ZX_EVENT_SIGNALED; }

 private:
  zx::event event_;
  uint64_t koid_;
  uint64_t local_id_ = 0;
};

}  // namespace magma

#endif  // SRC_GRAPHICS_LIB_MAGMA_SRC_MAGMA_UTIL_PLATFORM_ZIRCON_ZIRCON_PLATFORM_SEMAPHORE_H_
