// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/vcpu.h"

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

static thread_local Vcpu* thread_vcpu = nullptr;

#if __aarch64__
static zx_status_t HandleMemArm(const zx_packet_guest_mem_t& mem, uint64_t trap_key,
                                uint64_t* reg) {
  TRACE_DURATION("machina", "mmio", "addr", mem.addr, "access_size", mem.access_size);

  IoValue mmio = {mem.access_size, {.u64 = mem.data}};
  IoMapping* mapping = IoMapping::FromPortKey(trap_key);
  if (!mem.read) {
    return mapping->Write(mem.addr, mmio);
  }

  zx_status_t status = mapping->Read(mem.addr, &mmio);
  if (status != ZX_OK) {
    return status;
  }
  *reg = mmio.u64;
  if (mem.sign_extend && *reg & (1ul << (mmio.access_size * CHAR_BIT - 1))) {
    *reg |= UINT64_MAX << mmio.access_size;
  }
  return ZX_OK;
}
#elif __x86_64__
#include "src/virtualization/bin/vmm/arch/x64/decode.h"

static zx_status_t HandleMemX86(const zx_packet_guest_mem_t& mem, uint64_t trap_key,
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
#endif

Vcpu::Vcpu(uint64_t id, Guest* guest, zx_gpaddr_t entry, zx_gpaddr_t boot_ptr)
    : id_(id), guest_(guest), entry_(entry), boot_ptr_(boot_ptr) {}

zx_status_t Vcpu::Start() {
  std::promise<zx_status_t> barrier;
  std::future<zx_status_t> barrier_future = barrier.get_future();
  future_ = std::async(std::launch::async, fit::bind_member(this, &Vcpu::Loop), std::move(barrier));
  barrier_future.wait();
  return barrier_future.get();
}

zx_status_t Vcpu::Join() { return future_.get(); }

Vcpu* Vcpu::GetCurrent() {
  FX_DCHECK(thread_vcpu != nullptr) << "Thread does not have a VCPU";
  return thread_vcpu;
}

zx_status_t Vcpu::Loop(std::promise<zx_status_t> barrier) {
  FX_DCHECK(thread_vcpu == nullptr) << "Thread has multiple VCPUs";

  // Set the thread state.
  {
    thread_vcpu = this;
    auto name = fxl::StringPrintf("vcpu-%lu", id_);
    zx_status_t status = zx::thread::self()->set_property(ZX_PROP_NAME, name.c_str(), name.size());
    if (status != ZX_OK) {
      FX_LOGS(WARNING) << "Failed to set VCPU " << id_ << " thread name " << status;
    }
  }

  // Create the VCPU.
  {
    zx_status_t status = zx::vcpu::create(guest_->object(), 0, entry_, &vcpu_);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to create VCPU " << id_ << " " << status;
      barrier.set_value(status);
      return status;
    }
  }

  // Set the initial VCPU state.
  {
    zx_vcpu_state_t vcpu_state = {};
#if __aarch64__
    vcpu_state.x[0] = boot_ptr_;
#elif __x86_64__
    vcpu_state.rsi = boot_ptr_;
#endif

    zx_status_t status = vcpu_.write_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state));
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to set VCPU " << id_ << " state " << status;
      barrier.set_value(status);
      return status;
    }
  }

  // Unblock VCPU startup barrier.
  barrier.set_value(ZX_OK);

  while (true) {
    zx_port_packet_t packet;
    zx_status_t status = vcpu_.resume(&packet);
    if (status == ZX_ERR_STOP) {
      return ZX_OK;
    }
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to resume VCPU " << id_ << " " << status;
      exit(status);
    }

    status = HandlePacketLocked(packet);
    if (status == ZX_ERR_STOP) {
      return ZX_OK;
    }
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to handle packet " << packet.type << " " << status;
      exit(status);
    }
  }
}

zx_status_t Vcpu::Interrupt(uint32_t vector) {
  return vcpu_.interrupt(vector);
}

zx_status_t Vcpu::HandlePacketLocked(const zx_port_packet_t& packet) {
  switch (packet.type) {
    case ZX_PKT_TYPE_GUEST_MEM:
      return HandleMemLocked(packet.guest_mem, packet.key);
#if __x86_64__
    case ZX_PKT_TYPE_GUEST_IO:
      return HandleIo(packet.guest_io, packet.key);
#endif  // __x86_64__
    case ZX_PKT_TYPE_GUEST_VCPU:
      return HandleVcpu(packet.guest_vcpu, packet.key);
    default:
      FX_LOGS(ERROR) << "Unhandled guest packet " << packet.type;
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t Vcpu::HandleMemLocked(const zx_packet_guest_mem_t& mem, uint64_t trap_key) {
  zx_vcpu_state_t vcpu_state;
  zx_status_t status;
#if __aarch64__
  if (mem.read)
#endif
  {
    status = vcpu_.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state));
    if (status != ZX_OK) {
      return status;
    }
  }

  bool do_write = false;
#if __aarch64__
  do_write = mem.read;
  status = HandleMemArm(mem, trap_key, &vcpu_state.x[mem.xt]);
#elif __x86_64__
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
#endif  // __x86_64__

  if (status == ZX_OK && do_write) {
    return vcpu_.write_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state));
  }

  return status;
}

#if __x86_64__
zx_status_t Vcpu::HandleInput(const zx_packet_guest_io_t& io, uint64_t trap_key) {
  TRACE_DURATION("machina", "pio_in", "port", io.port, "access_size", io.access_size);

  IoValue value = {};
  value.access_size = io.access_size;
  zx_status_t status = IoMapping::FromPortKey(trap_key)->Read(io.port, &value);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to handle port in 0x" << std::hex << io.port << " " << std::dec
                   << status;
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

zx_status_t Vcpu::HandleOutput(const zx_packet_guest_io_t& io, uint64_t trap_key) {
  TRACE_DURATION("machina", "pio_out", "port", io.port, "access_size", io.access_size);

  IoValue value;
  value.access_size = io.access_size;
  value.u32 = io.u32;
  zx_status_t status = IoMapping::FromPortKey(trap_key)->Write(io.port, value);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to handle port out 0x" << std::hex << io.port << " " << std::dec
                   << status;
  }
  return status;
}

zx_status_t Vcpu::HandleIo(const zx_packet_guest_io_t& io, uint64_t trap_key) {
  return io.input ? HandleInput(io, trap_key) : HandleOutput(io, trap_key);
}
#endif  // __x86_64__

zx_status_t Vcpu::HandleVcpu(const zx_packet_guest_vcpu_t& packet, uint64_t trap_key) {
  switch (packet.type) {
    case ZX_PKT_GUEST_VCPU_INTERRUPT:
      return guest_->Interrupt(packet.interrupt.mask, packet.interrupt.vector);
    case ZX_PKT_GUEST_VCPU_STARTUP:
      return guest_->StartVcpu(packet.startup.id, packet.startup.entry, boot_ptr_);
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}
