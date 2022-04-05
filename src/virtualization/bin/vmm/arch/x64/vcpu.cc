// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/vcpu.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <zircon/status.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>

#include <page_tables/x86/constants.h>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/virtualization/bin/vmm/arch/x64/decode.h"
#include "src/virtualization/bin/vmm/arch/x64/page_table.h"
#include "src/virtualization/bin/vmm/guest.h"
#include "src/virtualization/bin/vmm/io.h"

namespace {

zx_status_t PerformMemAccess(const zx_packet_guest_mem_t& mem, IoMapping* device_mapping,
                             const Instruction& inst) {
  TRACE_DURATION("machina", "mmio", "addr", mem.addr, "access_size", inst.access_size);

  zx_status_t status;
  IoValue mmio = {inst.access_size, {.u64 = 0}};
  switch (inst.type) {
    case InstructionType::kWrite:
      switch (inst.access_size) {
        case 1:
          status = inst.Write(mmio.u8).status_value();
          break;
        case 2:
          status = inst.Write(mmio.u16).status_value();
          break;
        case 4:
          status = inst.Write(mmio.u32).status_value();
          break;
        default:
          return ZX_ERR_NOT_SUPPORTED;
      }
      if (status != ZX_OK) {
        return status;
      }
      return device_mapping->Write(mem.addr, mmio);

    case InstructionType::kRead:
      status = device_mapping->Read(mem.addr, &mmio);
      if (status != ZX_OK) {
        return status;
      }
      switch (inst.access_size) {
        case 1:
          return inst.Read(mmio.u8).status_value();
        case 2:
          return inst.Read(mmio.u16).status_value();
        case 4:
          return inst.Read(mmio.u32).status_value();
        default:
          return ZX_ERR_NOT_SUPPORTED;
      }

    case InstructionType::kTest:
      status = device_mapping->Read(mem.addr, &mmio);
      if (status != ZX_OK) {
        return status;
      }
      switch (inst.access_size) {
        case 1:
          return inst.Test8(static_cast<uint8_t>(inst.imm), mmio.u8).status_value();
        default:
          return ZX_ERR_NOT_SUPPORTED;
      }

    case InstructionType::kLogicalOr:
      status = device_mapping->Read(mem.addr, &mmio);
      if (status != ZX_OK) {
        return status;
      }
      switch (inst.access_size) {
        case 1:
          status = inst.Or(static_cast<uint8_t>(inst.imm), mmio.u8).status_value();
          break;
        case 2:
          status = inst.Or(static_cast<uint16_t>(inst.imm), mmio.u16).status_value();
          break;
        case 4:
          status = inst.Or(inst.imm, mmio.u32).status_value();
          break;
        default:
          return ZX_ERR_NOT_SUPPORTED;
      }
      if (status != ZX_OK) {
        return status;
      }
      return device_mapping->Write(mem.addr, mmio);

    default:
      return ZX_ERR_INVALID_ARGS;
  }
}

}  // namespace

zx_status_t Vcpu::ArchHandleMem(const zx_packet_guest_mem_t& mem, IoMapping* device_mapping) {
  // Read guest register state.
  zx_vcpu_state_t vcpu_state;
  zx_status_t status = vcpu_.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state));
  if (status != ZX_OK) {
    return status;
  }

  InstructionBuffer buffer;
  InstructionSpan span{buffer.begin(), mem.instruction_size};
  if (auto result = ReadInstruction(guest_->phys_mem(), mem.cr3, mem.rip, span);
      result.is_error()) {
    return result.status_value();
  }

  // Decode the instruction the guest was attempting to perform.
  auto inst = DecodeInstruction(span, mem.default_operand_size, vcpu_state);
  if (inst.is_error()) {
    std::string value;
    for (uint8_t i = 0; i < span.size(); i++) {
      fxl::StringAppendf(&value, " %hhx", span[i]);
    }
    FX_LOGS(ERROR) << "Unsupported instruction:" << value;
    return status;
  }

  // Perform the access.
  status = PerformMemAccess(mem, device_mapping, *inst);
  if (status != ZX_OK) {
    return status;
  }

  // If the operation was write-only and didn't change registers or flags, we are done.
  if (inst->type == InstructionType::kWrite) {
    return ZX_OK;
  }

  // Otherwise, update the guest's registers.
  return vcpu_.write_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state));
}

zx_status_t Vcpu::ArchHandleInput(const zx_packet_guest_io_t& io, IoMapping* device_mapping) {
  TRACE_DURATION("machina", "pio_in", "port", io.port, "access_size", io.access_size);

  IoValue value = {};
  value.access_size = io.access_size;
  zx_status_t status = device_mapping->Read(io.port, &value);
  if (status != ZX_OK) {
    return status;
  }

  zx_vcpu_io_t vcpu_io;
  memset(&vcpu_io, 0, sizeof(vcpu_io));
  vcpu_io.access_size = value.access_size;
  vcpu_io.u32 = value.u32;
  if (vcpu_io.access_size != io.access_size) {
    FX_LOGS(ERROR) << "Unexpected size (" << vcpu_io.access_size << " != " << io.access_size
                   << ") for port in 0x" << std::hex << io.port;
    return ZX_ERR_IO;
  }
  return vcpu_.write_state(ZX_VCPU_IO, &vcpu_io, sizeof(vcpu_io));
}

zx_status_t Vcpu::ArchHandleOutput(const zx_packet_guest_io_t& io, IoMapping* device_mapping) {
  TRACE_DURATION("machina", "pio_out", "port", io.port, "access_size", io.access_size);

  IoValue value;
  value.access_size = io.access_size;
  value.u32 = io.u32;
  return device_mapping->Write(io.port, value);
}

zx_status_t Vcpu::ArchHandleIo(const zx_packet_guest_io_t& io, uint64_t trap_key) {
  IoMapping* device_mapping = IoMapping::FromPortKey(trap_key);

  zx_status_t status =
      io.input ? ArchHandleInput(io, device_mapping) : ArchHandleOutput(io, device_mapping);

  // Print a warning for unknown errors.
  switch (status) {
    case ZX_OK:
    case ZX_ERR_CANCELED:
      break;

    default:
      FX_LOGS(ERROR) << std::hex << "Device '" << device_mapping->handler()->Name()
                     << "' returned status " << zx_status_get_string(status)
                     << " while attempting to handle IO port " << (io.input ? "read" : "write")
                     << " on port 0x" << io.port << " (mapping offset 0x"
                     << (io.port - device_mapping->base()) << ")";
      break;
  }

  return status;
}
