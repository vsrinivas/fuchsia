// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_platform_semaphore.h"

#include <lib/zx/time.h>

#include "magma_util/macros.h"
#include "platform_object.h"
#include "zircon_platform_port.h"

namespace magma {

bool ZirconPlatformSemaphore::duplicate_handle(uint32_t* handle_out) {
  zx::event duplicate;
  zx_status_t status = event_.duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate);
  if (status < 0)
    return DRETF(false, "zx_handle_duplicate failed: %d", status);
  *handle_out = duplicate.release();
  return true;
}

magma::Status ZirconPlatformSemaphore::WaitNoReset(uint64_t timeout_ms) {
  TRACE_DURATION("magma:sync", "semaphore wait", "id", koid_);
  zx_status_t status = event_.wait_one(
      zx_signal(), zx::deadline_after(zx::duration(magma::ms_to_signed_ns(timeout_ms))), nullptr);
  switch (status) {
    case ZX_OK:
      return MAGMA_STATUS_OK;
    case ZX_ERR_TIMED_OUT:
      return MAGMA_STATUS_TIMED_OUT;
    case ZX_ERR_CANCELED:
      return MAGMA_STATUS_CONNECTION_LOST;
    default:
      return DRET_MSG(MAGMA_STATUS_INTERNAL_ERROR, "Unexpected wait() status: %d", status);
  }
}

magma::Status ZirconPlatformSemaphore::Wait(uint64_t timeout_ms) {
  magma::Status status = WaitNoReset(timeout_ms);
  if (status.ok()) {
    Reset();
  }
  return status;
}

bool ZirconPlatformSemaphore::WaitAsync(PlatformPort* platform_port) {
  TRACE_DURATION("magma:sync", "semaphore wait async", "id", koid_);
  TRACE_FLOW_BEGIN("magma:sync", "semaphore wait async", koid_);

  auto port = static_cast<ZirconPlatformPort*>(platform_port);
  zx_status_t status = event_.wait_async(port->zx_port(), id(), zx_signal(), ZX_WAIT_ASYNC_ONCE);
  if (status != ZX_OK)
    return DRETF(false, "wait_async failed: %d", status);

  return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<PlatformSemaphore> PlatformSemaphore::Create() {
  zx::event event;
  zx_status_t status = zx::event::create(0, &event);
  if (status != ZX_OK)
    return DRETP(nullptr, "event::create failed: %d", status);

  uint64_t koid;
  if (!PlatformObject::IdFromHandle(event.get(), &koid))
    return DRETP(nullptr, "couldn't get koid from handle");

  return std::make_unique<ZirconPlatformSemaphore>(std::move(event), koid);
}

std::unique_ptr<PlatformSemaphore> PlatformSemaphore::Import(uint32_t handle) {
  zx::event event(handle);

  uint64_t koid;
  if (!PlatformObject::IdFromHandle(event.get(), &koid))
    return DRETP(nullptr, "couldn't get koid from handle");

  return std::make_unique<ZirconPlatformSemaphore>(std::move(event), koid);
}

}  // namespace magma
