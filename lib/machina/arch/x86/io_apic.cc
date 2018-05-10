// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/arch/x86/io_apic.h"

#include <string.h>

#include <fbl/auto_lock.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>

#include "garnet/lib/machina/address.h"
#include "garnet/lib/machina/bits.h"
#include "garnet/lib/machina/guest.h"
#include "garnet/lib/machina/vcpu.h"
#include "lib/fxl/logging.h"

// clang-format off

// IO APIC register addresses.
#define IO_APIC_IOREGSEL                0x00
#define IO_APIC_IOWIN                   0x10

// IO APIC register addresses.
#define IO_APIC_REGISTER_ID             0x00
#define IO_APIC_REGISTER_VER            0x01
#define IO_APIC_REGISTER_ARBITRATION    0x02

// IO APIC configuration constants.
#define IO_APIC_VERSION                 0x11
#define FIRST_REDIRECT_OFFSET           0x10
#define LAST_REDIRECT_OFFSET            (FIRST_REDIRECT_OFFSET + IoApic::kNumRedirectOffsets - 1)

// DESTMOD register.
#define IO_APIC_DESTMOD_PHYSICAL        0x00
#define IO_APIC_DESTMOD_LOGICAL         0x01

#define LOCAL_APIC_DFR_FLAT_MODEL       0xf

// clang-format on

namespace machina {

zx_status_t IoApic::Init(Guest* guest) {
  return guest->CreateMapping(TrapType::MMIO_SYNC, kIoApicPhysBase, kIoApicSize,
                              0, this);
}

zx_status_t IoApic::RegisterVcpu(uint8_t local_apic_id, Vcpu* vcpu) {
  if (local_apic_id >= kMaxVcpus) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (vcpus_[local_apic_id] != nullptr) {
    return ZX_ERR_ALREADY_EXISTS;
  }
  vcpus_[local_apic_id] = vcpu;
  return ZX_OK;
}

zx_status_t IoApic::SetRedirect(uint32_t global_irq, RedirectEntry& redirect) {
  if (global_irq >= kNumRedirects) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  fbl::AutoLock lock(&mutex_);
  redirect_[global_irq] = redirect;
  return ZX_OK;
}

zx_status_t IoApic::Interrupt(uint32_t global_irq) {
  if (global_irq >= kNumRedirects) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  RedirectEntry entry;
  {
    fbl::AutoLock lock(&mutex_);
    entry = redirect_[global_irq];
  }

  uint32_t vector = bits_shift(entry.lower, 7, 0);

  // The "destination mode" (DESTMOD) determines how the dest field in the
  // redirection entry should be interpreted.
  //
  // With a 'physical' mode, the destination is interpreted as the APIC ID
  // of the target APIC to receive the interrupt.
  //
  // With a 'logical' mode, the target depends on the 'logical destination
  // register'. In x2APIC mode this register is read-only and is derived from
  // the local APIC ID.
  //
  // See 82093AA (IOAPIC) Section 3.2.4.
  // See Intel Volume 3, Section 10.12.10
  uint32_t destmod = bit_shift(entry.lower, 11);
  if (destmod == IO_APIC_DESTMOD_PHYSICAL) {
    uint32_t dest = bits_shift(entry.upper, 27, 24);
    Vcpu* vcpu = dest < kMaxVcpus ? vcpus_[dest] : nullptr;
    if (vcpu == nullptr) {
      return ZX_ERR_NOT_FOUND;
    }
    return vcpu->Interrupt(vector);
  }

  // Logical DESTMOD.
  uint16_t dest = bits_shift(entry.upper, 31, 24);
  for (uint8_t local_apic_id = 0; local_apic_id < kMaxVcpus; ++local_apic_id) {
    // See Intel Volume 3, Section 10.12.10.2: logical ID = 1 << x2APIC ID[3:0].
    uint16_t logical_id = 1 << local_apic_id;
    if (!(logical_id & dest)) {
      continue;
    }
    Vcpu* vcpu = vcpus_[local_apic_id];
    if (vcpu == nullptr) {
      continue;
    }
    // Note we're not currently respecting the DELMODE field and
    // instead are only delivering to the fist local APIC that is
    // targeted.
    return vcpu->Interrupt(vector);
  }
  return ZX_ERR_NOT_FOUND;
}

zx_status_t IoApic::Read(uint64_t addr, IoValue* value) const {
  switch (addr) {
    case IO_APIC_IOREGSEL: {
      fbl::AutoLock lock(&mutex_);
      value->u32 = select_;
      return ZX_OK;
    }
    case IO_APIC_IOWIN: {
      uint32_t select_register;
      {
        fbl::AutoLock lock(&mutex_);
        select_register = select_;
      }
      return ReadRegister(select_register, value);
    }
    default:
      FXL_LOG(ERROR) << "Unhandled IO APIC read 0x" << std::hex << addr;
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t IoApic::Write(uint64_t addr, const IoValue& value) {
  switch (addr) {
    case IO_APIC_IOREGSEL: {
      if (value.u32 > UINT8_MAX) {
        return ZX_ERR_INVALID_ARGS;
      }
      fbl::AutoLock lock(&mutex_);
      select_ = value.u32;
      return ZX_OK;
    }
    case IO_APIC_IOWIN: {
      uint32_t select_register;
      {
        fbl::AutoLock lock(&mutex_);
        select_register = select_;
      }
      return WriteRegister(select_register, value);
    }
    default:
      FXL_LOG(ERROR) << "Unhandled IO APIC write 0x" << std::hex << addr;
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t IoApic::ReadRegister(uint32_t select_register,
                                 IoValue* value) const {
  switch (select_register) {
    case IO_APIC_REGISTER_ID: {
      fbl::AutoLock lock(&mutex_);
      value->u32 = id_;
      return ZX_OK;
    }
    case IO_APIC_REGISTER_VER:
      // There are two redirect offsets per redirection entry. We return
      // the maximum redirection entry index.
      //
      // From Intel 82093AA, Section 3.2.2.
      value->u32 = (kNumRedirects - 1) << 16 | IO_APIC_VERSION;
      return ZX_OK;
    case IO_APIC_REGISTER_ARBITRATION:
      // Since we have a single I/O APIC, it is always the winner
      // of arbitration and its arbitration register is always 0.
      value->u32 = 0;
      return ZX_OK;
    case FIRST_REDIRECT_OFFSET ... LAST_REDIRECT_OFFSET: {
      fbl::AutoLock lock(&mutex_);
      uint32_t redirect_offset = select_ - FIRST_REDIRECT_OFFSET;
      const RedirectEntry& entry = redirect_[redirect_offset / 2];
      uint32_t redirect_register =
          redirect_offset % 2 == 0 ? entry.lower : entry.upper;
      value->u32 = redirect_register;
      return ZX_OK;
    }
    default:
      FXL_LOG(ERROR) << "Unhandled IO APIC register read 0x" << std::hex
                     << select_register;
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t IoApic::WriteRegister(uint32_t select_register,
                                  const IoValue& value) {
  switch (select_register) {
    case IO_APIC_REGISTER_ID: {
      fbl::AutoLock lock(&mutex_);
      id_ = value.u32;
      return ZX_OK;
    }
    case FIRST_REDIRECT_OFFSET ... LAST_REDIRECT_OFFSET: {
      fbl::AutoLock lock(&mutex_);
      uint32_t redirect_offset = select_ - FIRST_REDIRECT_OFFSET;
      RedirectEntry& entry = redirect_[redirect_offset / 2];
      uint32_t* redirect_register =
          redirect_offset % 2 == 0 ? &entry.lower : &entry.upper;
      *redirect_register = value.u32;
      return ZX_OK;
    }
    case IO_APIC_REGISTER_VER:
    case IO_APIC_REGISTER_ARBITRATION:
      // Read-only, ignore writes.
      return ZX_OK;
    default:
      FXL_LOG(ERROR) << "Unhandled IO APIC register write 0x" << std::hex
                     << select_register;
      return ZX_ERR_NOT_SUPPORTED;
  }
}

}  // namespace machina
