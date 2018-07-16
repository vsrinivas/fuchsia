// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/io.h"

#include <zircon/syscalls/hypervisor.h>

#include "garnet/lib/machina/guest.h"
#include "lib/fxl/logging.h"

namespace machina {

static constexpr IoValue kBellValue = {};

IoMapping::IoMapping(uint32_t kind, uint64_t base, size_t size, uint64_t offset,
                     IoHandler* handler)
    : kind_(kind),
      base_(base),
      size_(size),
      offset_(offset),
      handler_(handler),
      async_trap_(this) {}

zx_status_t IoMapping::SetTrap(Guest* guest) {
  if (kind_ == ZX_GUEST_TRAP_BELL) {
    return async_trap_.SetTrap(guest->device_dispatcher(),
                               *zx::unowned_guest(guest->handle()), base_,
                               size_);
  } else {
    uintptr_t key = reinterpret_cast<uintptr_t>(this);
    return zx_guest_set_trap(guest->handle(), kind_, base_, size_,
                             ZX_HANDLE_INVALID, key);
  }
}

void IoMapping::CallIoHandlerAsync(async_dispatcher_t* dispatcher,
                                   async::GuestBellTrapBase* trap,
                                   zx_status_t status,
                                   const zx_packet_guest_bell_t* bell) {
  FXL_CHECK(Write(bell->addr, kBellValue) == ZX_OK)
      << "Failed to handle async IO";
}

}  // namespace machina
