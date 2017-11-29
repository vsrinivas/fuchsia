// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/arch/arm64/pl031.h"

#include <stdio.h>

#include <hypervisor/address.h>
#include <hypervisor/guest.h>

// PL031 registers.
enum class Pl031Register : uint64_t {
    DR = 0x00,
};

zx_status_t Pl031::Init(Guest* guest) {
  return guest->CreateMapping(TrapType::MMIO_SYNC, PL031_PHYS_BASE, PL031_SIZE,
                              0, this);
}

zx_status_t Pl031::Read(uint64_t addr, IoValue* value) const {
  switch (static_cast<Pl031Register>(addr)) {
    case Pl031Register::DR:
      if (value->access_size != 4)
        return ZX_ERR_IO_DATA_INTEGRITY;
      value->u32 = zx_time_get(ZX_CLOCK_UTC);
      return ZX_OK;
    default:
      fprintf(stderr, "Unhandled PL031 address read %#lx\n", addr);
      return ZX_ERR_IO;
  }
}

zx_status_t Pl031::Write(uint64_t addr, const IoValue& value) {
  fprintf(stderr, "Unhandled PL031 address write %#lx\n", addr);
  return ZX_ERR_IO;
}
