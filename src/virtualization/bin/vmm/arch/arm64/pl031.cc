// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/arch/arm64/pl031.h"

#include <endian.h>
#include <stdio.h>

#include "src/lib/syslog/cpp/logger.h"
#include "src/virtualization/bin/vmm/guest.h"
#include "src/virtualization/bin/vmm/rtc.h"

__BEGIN_CDECLS;
#include <libfdt.h>
__END_CDECLS;

// PL031 registers.
enum class Pl031Register : uint64_t {
  DR = 0x00,
};

static constexpr uint64_t kPl031PhysBase = 0x808301000;
static constexpr uint64_t kPl031Size = 0x1000;

zx_status_t Pl031::Init(Guest* guest) {
  return guest->CreateMapping(TrapType::MMIO_SYNC, kPl031PhysBase, kPl031Size, 0, this);
}

zx_status_t Pl031::Read(uint64_t addr, IoValue* value) const {
  switch (static_cast<Pl031Register>(addr)) {
    case Pl031Register::DR:
      if (value->access_size != 4) {
        return ZX_ERR_IO_DATA_INTEGRITY;
      }
      value->u32 = rtc_time();
      return ZX_OK;
    default:
      FX_LOGS(ERROR) << "Unhandled PL031 address read 0x" << std::hex << addr;
      return ZX_ERR_IO;
  }
}

zx_status_t Pl031::Write(uint64_t addr, const IoValue& value) {
  FX_LOGS(ERROR) << "Unhandled PL031 address write 0x" << std::hex << addr;
  return ZX_ERR_IO;
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
