// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/arch/arm64/pl031.h"

#include <endian.h>
#include <lib/syslog/cpp/macros.h>
#include <stdio.h>
#include <time.h>

#include "src/virtualization/bin/vmm/guest.h"

__BEGIN_CDECLS;
#include <libfdt.h>
__END_CDECLS;

// PL031 registers.
enum class Pl031Register : uint64_t {
  DR = 0x00,  // Data register
  CR = 0x0c,  // Control register
};

// Control register bit definitions.
//
// See ARM PrimeCell Real Time Clock (PL031), Revision r1p3.
// Section 3.3.4 Control Register, RTCCR.
enum ControlRegister {
  // Bits [31:1] reserved.
  kCrRtcStart = 1u << 0,
};

static constexpr uint64_t kPl031PhysBase = 0x808301000;
static constexpr uint64_t kPl031Size = 0x1000;

zx_status_t Pl031::Init(Guest* guest) {
  return guest->CreateMapping(TrapType::MMIO_SYNC, kPl031PhysBase, kPl031Size, 0, this);
}

zx_status_t Pl031::Read(uint64_t addr, IoValue* value) {
  // We only support 32-bit reads/writes.
  if (value->access_size != 4) {
    return ZX_ERR_IO;
  }

  switch (static_cast<Pl031Register>(addr)) {
    case Pl031Register::DR:
      value->u32 = static_cast<uint32_t>(time(nullptr));
      return ZX_OK;

    case Pl031Register::CR:
      value->u32 = 0;
      return ZX_OK;

    default:
      FX_LOGS(WARNING) << "Unhandled PL031 address read 0x" << std::hex << addr;
      value->u32 = 0;
      return ZX_OK;
  }
}

zx_status_t Pl031::Write(uint64_t addr, const IoValue& value) {
  // We only support 32-bit reads/writes.
  if (value.access_size != 4) {
    return ZX_ERR_IO;
  }

  switch (static_cast<Pl031Register>(addr)) {
    case Pl031Register::CR:
      // We only support enabling the RTC. Warn on any other value.
      if (value.u32 != ControlRegister::kCrRtcStart) {
        FX_LOGS(WARNING) << "Unsupported value 0x" << std::hex << value.u32
                         << " written to PL031 control register. Ignoring";
      }
      return ZX_OK;

    default:
      FX_LOGS(WARNING) << "Unsupported write to PL031 register 0x" << std::hex << addr;
      return ZX_OK;
  }
}

zx_status_t Pl031::ConfigureDtb(void* dtb) const {
  uint64_t reg_val[2] = {htobe64(kPl031PhysBase), htobe64(kPl031Size)};
  int node_off = fdt_node_offset_by_prop_value(dtb, -1, "reg", reg_val, sizeof(reg_val));
  if (node_off < 0) {
    FX_LOGS(ERROR) << "Failed to find PL031 in DTB";
    return ZX_ERR_INTERNAL;
  }
  int ret = fdt_node_check_compatible(dtb, node_off, "arm,pl031");
  if (ret != 0) {
    FX_LOGS(ERROR) << "Device with PL031 registers is not PL031 compatible";
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}
