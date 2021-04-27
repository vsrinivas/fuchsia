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
#include "src/virtualization/bin/vmm/arch/x64/decode.h"
#include "src/virtualization/bin/vmm/guest.h"
#include "src/virtualization/bin/vmm/io.h"

namespace {

zx_status_t HandleMemX86(const zx_packet_guest_mem_t& mem, uint64_t trap_key,
                         const Instruction* inst) {
  TRACE_DURATION("machina", "mmio", "addr", mem.addr, "access_size", inst->access_size);

  zx_status_t status;
  IoValue mmio = {inst->access_size, {.u64 = 0}};
  switch (inst->type) {
    case INST_MOV_WRITE:
      switch (inst->access_size) {
        case 1:
          status = inst_write8(inst, &mmio.u8);
          break;
        case 2:
          status = inst_write16(inst, &mmio.u16);
          break;
        case 4:
          status = inst_write32(inst, &mmio.u32);
          break;
        default:
          return ZX_ERR_NOT_SUPPORTED;
      }
      if (status != ZX_OK) {
        return status;
      }
      return IoMapping::FromPortKey(trap_key)->Write(mem.addr, mmio);

    case INST_MOV_READ:
      status = IoMapping::FromPortKey(trap_key)->Read(mem.addr, &mmio);
      if (status != ZX_OK) {
        return status;
      }
      switch (inst->access_size) {
        case 1:
          return inst_read8(inst, mmio.u8);
        case 2:
          return inst_read16(inst, mmio.u16);
        case 4:
          return inst_read32(inst, mmio.u32);
        default:
          return ZX_ERR_NOT_SUPPORTED;
      }

    case INST_TEST:
      status = IoMapping::FromPortKey(trap_key)->Read(mem.addr, &mmio);
      if (status != ZX_OK) {
        return status;
      }
      switch (inst->access_size) {
        case 1:
          return inst_test8(inst, static_cast<uint8_t>(inst->imm), mmio.u8);
        default:
          return ZX_ERR_NOT_SUPPORTED;
      }

    default:
      return ZX_ERR_INVALID_ARGS;
  }
}
}  // namespace

zx_status_t Vcpu::ArchHandleMem(const zx_packet_guest_mem_t& mem, uint64_t trap_key) {
  zx_vcpu_state_t vcpu_state;
  zx_status_t status;
  {
    status = vcpu_.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state));
    if (status != ZX_OK) {
      return status;
    }
  }

  bool do_write = false;
  Instruction inst;
  status = inst_decode(mem.inst_buf, mem.inst_len, mem.default_operand_size, &vcpu_state, &inst);
  if (status != ZX_OK) {
    std::string inst;
    for (uint8_t i = 0; i < mem.inst_len; i++) {
      fxl::StringAppendf(&inst, " %x", mem.inst_buf[i]);
    }
    FX_LOGS(ERROR) << "Unsupported instruction:" << inst;
  } else {
    status = HandleMemX86(mem, trap_key, &inst);
    // If there was an attempt to read or test memory, update the GPRs.
    do_write = inst.type == INST_MOV_READ || inst.type == INST_TEST;
  }

  if (status == ZX_OK && do_write) {
    return vcpu_.write_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state));
  }

  return status;
}

zx_status_t Vcpu::ArchHandleInput(const zx_packet_guest_io_t& io, uint64_t trap_key) {
  TRACE_DURATION("machina", "pio_in", "port", io.port, "access_size", io.access_size);

  IoValue value = {};
  value.access_size = io.access_size;
  zx_status_t status = IoMapping::FromPortKey(trap_key)->Read(io.port, &value);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to handle port in 0x" << std::hex << io.port;
    return status;
  }

  zx_vcpu_io_t vcpu_io;
  memset(&vcpu_io, 0, sizeof(vcpu_io));
  vcpu_io.access_size = value.access_size;
  vcpu_io.u32 = value.u32;
  if (vcpu_io.access_size != io.access_size) {
    FX_LOGS(ERROR) << "Unexpected size (" << vcpu_io.access_size << " != " << io.access_size
                   << ") for port in 0x" << std::hex << io.port;
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  return vcpu_.write_state(ZX_VCPU_IO, &vcpu_io, sizeof(vcpu_io));
}

zx_status_t Vcpu::ArchHandleOutput(const zx_packet_guest_io_t& io, uint64_t trap_key) {
  TRACE_DURATION("machina", "pio_out", "port", io.port, "access_size", io.access_size);

  IoValue value;
  value.access_size = io.access_size;
  value.u32 = io.u32;
  zx_status_t status = IoMapping::FromPortKey(trap_key)->Write(io.port, value);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to handle port out 0x" << std::hex << io.port;
  }
  return status;
}

zx_status_t Vcpu::ArchHandleIo(const zx_packet_guest_io_t& io, uint64_t trap_key) {
  return io.input ? ArchHandleInput(io, trap_key) : ArchHandleOutput(io, trap_key);
}
