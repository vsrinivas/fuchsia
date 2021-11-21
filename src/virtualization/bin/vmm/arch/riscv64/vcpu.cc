// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/vcpu.h"

#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/zx/thread.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/virtualization/bin/vmm/guest.h"
#include "src/virtualization/bin/vmm/io.h"

namespace {

zx_status_t PerformMemAccess(const zx_packet_guest_mem_t& mem, IoMapping* device_mapping,
                             uint64_t* reg) {
  TRACE_DURATION("machina", "mmio", "addr", mem.addr, "access_size", mem.access_size);

  IoValue mmio = {mem.access_size, {.u64 = mem.data}};
  if (!mem.read) {
    return device_mapping->Write(mem.addr, mmio);
  }

  zx_status_t status = device_mapping->Read(mem.addr, &mmio);
  if (status != ZX_OK) {
    return status;
  }
  *reg = mmio.u64;
  if (mem.sign_extend && *reg & (1ul << (mmio.access_size * CHAR_BIT - 1))) {
    *reg |= UINT64_MAX << mmio.access_size;
  }
  return ZX_OK;
}

}  // namespace

zx_status_t Vcpu::ArchHandleMem(const zx_packet_guest_mem_t& mem, IoMapping* device_mapping) {
  // Perform the access.
  uint64_t read_value;
  zx_status_t status = PerformMemAccess(mem, device_mapping, &read_value);
  if (status != ZX_OK) {
    return status;
  }

  // If the guest was reading from the MMIO, update their register set
  // to contain the read value.
  if (mem.read) {
    // Read.
    zx_vcpu_state_t vcpu_state;
    status = vcpu_.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state));
    if (status != ZX_OK) {
      return status;
    }

    // Update the register set.
    vcpu_state.x[mem.xt] = read_value;

    // Write.
    status = vcpu_.write_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state));
    if (status != ZX_OK) {
      return status;
    }
  }

  return ZX_OK;
}
