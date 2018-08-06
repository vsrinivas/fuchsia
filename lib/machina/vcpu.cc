// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/vcpu.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <fbl/auto_lock.h>
#include <fbl/string_buffer.h>
#include <trace/event.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>

#include "garnet/lib/machina/guest.h"
#include "garnet/lib/machina/io.h"
#include "lib/fxl/logging.h"

#ifdef __x86_64__
#include "garnet/lib/machina/arch/x86/decode.h"
#endif

namespace machina {

thread_local Vcpu* thread_vcpu = nullptr;

#if __aarch64__
static zx_status_t HandleMmioArm(const zx_packet_guest_mem_t& mem,
                                 uint64_t trap_key, uint64_t* reg) {
  TRACE_DURATION("machina", "mmio", "addr", mem.addr, "access_size",
                 mem.access_size);

  machina::IoValue mmio = {mem.access_size, {.u64 = mem.data}};
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
static zx_status_t HandleMmioX86(const zx_packet_guest_mem_t& mem,
                                 uint64_t trap_key,
                                 const machina::Instruction* inst) {
  TRACE_DURATION("machina", "mmio", "addr", mem.addr, "access_size",
                 inst->access_size);

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

struct Vcpu::ThreadEntryArgs {
  Guest* guest;
  Vcpu* vcpu;
  zx_vaddr_t entry;
};

zx_status_t Vcpu::Create(Guest* guest, zx_vaddr_t entry, uint64_t id) {
  guest_ = guest;
  id_ = id;
  ThreadEntryArgs args = {
      .guest = guest,
      .vcpu = this,
      .entry = entry,
  };
  fbl::StringBuffer<ZX_MAX_NAME_LEN> name_buffer;
  name_buffer.AppendPrintf("vcpu-%lu", id);
  auto thread_entry = [](void* arg) {
    ThreadEntryArgs* thread_args = reinterpret_cast<ThreadEntryArgs*>(arg);
    return thread_args->vcpu->ThreadEntry(thread_args);
  };
  int ret =
      thrd_create_with_name(&thread_, thread_entry, &args, name_buffer.c_str());
  if (ret != thrd_success) {
    return ZX_ERR_INTERNAL;
  }

  fbl::AutoLock lock(&mutex_);
  WaitForStateChangeLocked(State::UNINITIALIZED);
  if (state_ != State::WAITING_TO_START) {
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

Vcpu* Vcpu::GetCurrent() {
  FXL_DCHECK(thread_vcpu != nullptr) << "Thread does not have a VCPU";
  return thread_vcpu;
}

zx_status_t Vcpu::ThreadEntry(const ThreadEntryArgs* args) {
  {
    fbl::AutoLock lock(&mutex_);
    if (state_ != State::UNINITIALIZED) {
      return ZX_ERR_BAD_STATE;
    }

    zx_status_t status =
        zx::vcpu::create(*args->guest->object(), 0, args->entry, &vcpu_);
    if (status != ZX_OK) {
      SetStateLocked(State::ERROR_FAILED_TO_CREATE);
      return status;
    }

    SetStateLocked(State::WAITING_TO_START);
    WaitForStateChangeLocked(State::WAITING_TO_START);
    if (state_ != State::STARTING) {
      return ZX_ERR_BAD_STATE;
    }

    if (initial_vcpu_state_ != nullptr) {
      status = vcpu_.write_state(ZX_VCPU_STATE, initial_vcpu_state_,
                                 sizeof(*initial_vcpu_state_));
      if (status != ZX_OK) {
        SetStateLocked(State::ERROR_FAILED_TO_START);
        return status;
      }
    }

    SetStateLocked(State::STARTED);
  }

  return Loop();
}

void Vcpu::SetStateLocked(State new_state) {
  state_ = new_state;
  cnd_signal(&state_cnd_);
}

void Vcpu::WaitForStateChangeLocked(State initial_state) {
  while (state_ == initial_state) {
    cnd_wait(&state_cnd_, mutex_.GetInternal());
  }
}

void Vcpu::SetState(State new_state) {
  fbl::AutoLock lock(&mutex_);
  SetStateLocked(new_state);
}

zx_status_t Vcpu::Loop() {
  FXL_DCHECK(thread_vcpu == nullptr) << "Thread has multiple VCPUs";
  thread_vcpu = this;
  zx_port_packet_t packet;
  while (true) {
    zx_status_t status = vcpu_.resume(&packet);
    if (status == ZX_ERR_STOP) {
      SetState(State::TERMINATED);
      return ZX_OK;
    }
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to resume VCPU-" << id_ << ": " << status;
      exit(status);
    }

    status = HandlePacket(packet);
    if (status == ZX_ERR_STOP) {
      SetState(State::TERMINATED);
      return ZX_OK;
    }
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to handle packet " << packet.type << ": "
                     << status;
      exit(status);
    }
  }
}

zx_status_t Vcpu::Start(zx_vcpu_state_t* initial_vcpu_state) {
  fbl::AutoLock lock(&mutex_);
  if (state_ != State::WAITING_TO_START) {
    return ZX_ERR_BAD_STATE;
  }

  // Place the VCPU in the |STARTING| state which will cause the VCPU to
  // write the initial state and begin VCPU execution.
  initial_vcpu_state_ = initial_vcpu_state;
  SetStateLocked(State::STARTING);
  WaitForStateChangeLocked(State::STARTING);
  if (state_ != State::STARTED) {
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

zx_status_t Vcpu::Join() {
  zx_status_t vcpu_result = ZX_ERR_INTERNAL;
  int ret = thrd_join(thread_, &vcpu_result);
  return ret == thrd_success ? vcpu_result : ZX_ERR_INTERNAL;
}

zx_status_t Vcpu::Interrupt(uint32_t vector) { return vcpu_.interrupt(vector); }

zx_status_t Vcpu::HandlePacket(const zx_port_packet_t& packet) {
  switch (packet.type) {
    case ZX_PKT_TYPE_GUEST_MEM:
      return HandleMem(packet.guest_mem, packet.key);
#if __x86_64__
    case ZX_PKT_TYPE_GUEST_IO:
      return HandleIo(packet.guest_io, packet.key);
#endif  // __x86_64__
    case ZX_PKT_TYPE_GUEST_VCPU:
      return HandleVcpu(packet.guest_vcpu, packet.key);
    default:
      FXL_LOG(ERROR) << "Unhandled guest packet " << packet.type;
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t Vcpu::HandleMem(const zx_packet_guest_mem_t& mem,
                            uint64_t trap_key) {
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
  status = HandleMmioArm(mem, trap_key, &vcpu_state.x[mem.xt]);
#elif __x86_64__
  Instruction inst;
  status = inst_decode(mem.inst_buf, mem.inst_len, mem.default_operand_size,
                       &vcpu_state, &inst);
  if (status != ZX_OK) {
    fbl::StringBuffer<LINE_MAX> buffer;
    for (uint8_t i = 0; i < mem.inst_len; i++) {
      buffer.AppendPrintf(" %x", mem.inst_buf[i]);
    }
    FXL_LOG(ERROR) << "Unsupported instruction:" << buffer.c_str();
  } else {
    status = HandleMmioX86(mem, trap_key, &inst);
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
zx_status_t Vcpu::HandleInput(const zx_packet_guest_io_t& io,
                              uint64_t trap_key) {
  TRACE_DURATION("machina", "pio_in", "port", io.port, "access_size",
                 io.access_size);

  IoValue value = {};
  value.access_size = io.access_size;
  zx_status_t status = IoMapping::FromPortKey(trap_key)->Read(io.port, &value);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to handle port in 0x" << std::hex << io.port
                   << ": " << std::dec << status;
    return status;
  }

  zx_vcpu_io_t vcpu_io;
  memset(&vcpu_io, 0, sizeof(vcpu_io));
  vcpu_io.access_size = value.access_size;
  vcpu_io.u32 = value.u32;
  if (vcpu_io.access_size != io.access_size) {
    FXL_LOG(ERROR) << "Unexpected size (" << vcpu_io.access_size
                   << " != " << io.access_size << ") for port in 0x" << std::hex
                   << io.port;
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  return vcpu_.write_state(ZX_VCPU_IO, &vcpu_io, sizeof(vcpu_io));
}

zx_status_t Vcpu::HandleOutput(const zx_packet_guest_io_t& io,
                               uint64_t trap_key) {
  TRACE_DURATION("machina", "pio_out", "port", io.port, "access_size",
                 io.access_size);

  IoValue value;
  value.access_size = io.access_size;
  value.u32 = io.u32;
  zx_status_t status = IoMapping::FromPortKey(trap_key)->Write(io.port, value);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to handle port out 0x" << std::hex << io.port
                   << ": " << std::dec << status;
  }
  return status;
}

zx_status_t Vcpu::HandleIo(const zx_packet_guest_io_t& io, uint64_t trap_key) {
  return io.input ? HandleInput(io, trap_key) : HandleOutput(io, trap_key);
}
#endif  // __x86_64__

zx_status_t Vcpu::HandleVcpu(const zx_packet_guest_vcpu_t& packet,
                             uint64_t trap_key) {
  switch (packet.type) {
    case ZX_PKT_GUEST_VCPU_INTERRUPT:
      return guest_->SignalInterrupt(packet.interrupt.mask,
                                     packet.interrupt.vector);
    case ZX_PKT_GUEST_VCPU_STARTUP:
      if (id_ != 0) {
        FXL_LOG(ERROR)
            << "Secondary processors must be started by the primary processor";
        return ZX_ERR_BAD_STATE;
      }
      return guest_->StartVcpu(packet.startup.entry, packet.startup.id);
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

}  // namespace machina
