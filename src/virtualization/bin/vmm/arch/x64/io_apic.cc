// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/arch/x64/io_apic.h"

#include <lib/syslog/cpp/macros.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>

#include "src/virtualization/bin/vmm/arch/x64/io_apic_registers.h"
#include "src/virtualization/bin/vmm/bits.h"
#include "src/virtualization/bin/vmm/guest.h"
#include "src/virtualization/bin/vmm/vcpu.h"

static constexpr uint64_t kMemSize = 0x1000;

IoApic::IoApic(Guest* guest, InterruptCallback interrupt)
    : guest_(guest), interrupt_fn_(std::move(interrupt)) {}

IoApic::IoApic(Guest* guest)
    : guest_(guest), interrupt_fn_([guest](uint64_t mask, uint32_t vector) -> zx_status_t {
        return guest->Interrupt(mask, vector);
      }) {}

zx_status_t IoApic::Init() {
  return guest_->CreateMapping(TrapType::MMIO_SYNC, kPhysBase, kMemSize, 0, this);
}

zx_status_t IoApic::Interrupt(uint32_t global_irq) {
  if (global_irq >= kNumInterrupts) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  IoApicRedirectEntry entry;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    InputInterrupt& interrupt = input_interrupts_[global_irq];

    // If the interrupt is masked, mark it as pending, but don't deliver it.
    if (interrupt.entry.mask()) {
      interrupt.pending = true;
      return ZX_OK;
    }

    entry = interrupt.entry;
  }

  return DeliverInterrupt(entry);
}

zx_status_t IoApic::Read(uint64_t addr, IoValue* value) {
  switch (addr) {
    case kIoApicIoRegSel: {
      std::lock_guard<std::mutex> lock(mutex_);
      value->u32 = select_;
      return ZX_OK;
    }
    case kIoApicIoWin: {
      std::lock_guard<std::mutex> lock(mutex_);
      return ReadRegisterLocked(select_, value);
    }
    case kIoApicEOIR: {
      value->u32 = 0;
      return ZX_OK;
    }
    default:
      FX_LOGS(ERROR) << "Unhandled IO APIC read 0x" << std::hex << addr;
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t IoApic::Write(uint64_t addr, const IoValue& value) {
  switch (addr) {
    case kIoApicIoRegSel: {
      std::lock_guard<std::mutex> lock(mutex_);
      select_ = value.u8;
      return ZX_OK;
    }
    case kIoApicIoWin: {
      zx::result<Action> result;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        result = WriteRegisterLocked(select_, value);
      }
      if (!result.is_ok()) {
        return result.status_value();
      }

      // If writing to a register caused an interrupt to fire (e.g.,
      // unmasking an interrupt), deliver it now.
      if (result.value()) {
        DeliverInterrupt(*result.value());
      }

      return ZX_OK;
    }
    case kIoApicEOIR: {
      // End of interrupt.
      //
      // For level-triggered interrupts, the OS may indicate to the IO APIC
      // the interrupt has finished, and if the level is still high it should
      // be considered a new interrupt.
      //
      // We internally only use edge-triggered interrupts (the "edge" being
      // when our `Interrupt` function is called), so we can ignore writes to
      // this register.
      //
      // TODO(fxbug.dev/77786): Correctly support level-triggered interrupts.
      return ZX_OK;
    }
    default:
      FX_LOGS(ERROR) << "Unhandled IO APIC write 0x" << std::hex << addr;
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t IoApic::ReadRegisterLocked(uint8_t select_register, IoValue* value) const {
  switch (select_register) {
    case kIoApicRegisterId: {
      value->u32 = id_;
      return ZX_OK;
    }
    case kIoApicRegisterVer:
      // There are two redirect offsets per redirection entry. We return
      // the maximum redirection entry index.
      //
      // From Intel ICH10, Section 13.5.6.
      value->u32 = (kNumInterrupts - 1) << 16 | kIoApicVersion;
      return ZX_OK;
    case kIoApicRegisterArbitration:
      // Since we have a single I/O APIC, it is always the winner
      // of arbitration and its arbitration register is always 0.
      value->u32 = 0;
      return ZX_OK;
    case kFirstRedirectOffset ... kLastRedirectOffset: {
      uint32_t redirect_offset = select_register - kFirstRedirectOffset;
      uint32_t global_irq = redirect_offset / 2;
      RedirectBits bits = redirect_offset % 2 == 0 ? RedirectBits::kLower : RedirectBits::kUpper;
      return ReadRedirectEntryLocked(global_irq, bits, value);
    }
    default:
      FX_LOGS(ERROR) << "Unhandled IO APIC register read 0x" << std::hex << select_register;
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx::result<IoApic::Action> IoApic::WriteRegisterLocked(uint8_t select_register,
                                                       const IoValue& value) {
  switch (select_register) {
    case kIoApicRegisterId: {
      id_ = value.u32;
      return zx::ok(std::nullopt);
    }
    case kFirstRedirectOffset ... kLastRedirectOffset: {
      uint32_t redirect_offset = select_register - kFirstRedirectOffset;
      uint32_t global_irq = redirect_offset / 2;
      RedirectBits bits = redirect_offset % 2 == 0 ? RedirectBits::kLower : RedirectBits::kUpper;
      return WriteRedirectEntryLocked(global_irq, bits, value);
    }
    case kIoApicRegisterVer:
    case kIoApicRegisterArbitration:
      // Read-only, ignore writes.
      return zx::ok(std::nullopt);
    default:
      FX_LOGS(ERROR) << "Unhandled IO APIC register write 0x" << std::hex << select_register;
      return zx::error(ZX_ERR_NOT_SUPPORTED);
  }
}

zx_status_t IoApic::ReadRedirectEntryLocked(uint32_t global_irq, RedirectBits bits,
                                            IoValue* result) const {
  if (result->access_size != 4) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  const IoApicRedirectEntry& entry = input_interrupts_[global_irq].entry;
  result->u32 = static_cast<uint32_t>(bits == RedirectBits::kLower ? entry.lower() : entry.upper());
  return ZX_OK;
}

zx::result<IoApic::Action> IoApic::WriteRedirectEntryLocked(uint32_t global_irq, RedirectBits bits,
                                                            const IoValue& value) {
  if (value.access_size != 4) {
    return zx::error(ZX_ERR_NOT_SUPPORTED);
  }

  // Update the chosen 32 bits of the 64 bit register.
  InputInterrupt& interrupt = input_interrupts_[global_irq];
  if (bits == RedirectBits::kLower) {
    interrupt.entry.set_lower(value.u32);
  } else {
    interrupt.entry.set_upper(value.u32);
  }

  // Report any pending and now unmasked interrupt to the caller.
  //
  // TODO(fxbug.dev/77786): We do not correctly support level-triggered interrupts
  // here. In particular, the current IO APIC API only supports
  // edge-triggered interrupts (i.e., when our `Interrupt` method is called),
  // but we never find out when an interrupt stops being active.
  //
  // The result is that we can't be sure that the pending interrupt
  // really is still pending. We opt to deliver it anyway, possibly
  // generating a spurious interrupt.
  if (interrupt.pending && interrupt.entry.mask() == 0) {
    interrupt.pending = false;
    return zx::ok(interrupt.entry);
  }

  return zx::ok(std::nullopt);
}

zx_status_t IoApic::DeliverInterrupt(const IoApicRedirectEntry& entry) {
  uint32_t vector = static_cast<uint32_t>(entry.vector());

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
  // See Intel ICH10 Section 13.5.7.
  // See Intel Volume 3, Section 10.12.10
  if (entry.destination_mode() == kIoApicDestmodPhysical) {
    uint64_t dest = entry.destination();

    // Ensure that the top bits of dest are zero. From ICH10 Section 13.5.7:
    // "If bit 11 of this entry is 0 (Physical), then bits 59:56 specifies an
    // APIC ID. In this case, bits 63:59 should be programmed by software to
    // 0."
    if (dest >= kIoApicNumPhysicalDestinations) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    return interrupt_fn_(1ul << dest, vector);
  }

  // Logical DESTMOD. See Intel Volume 3, Section 10.12.10.2:
  // logical ID = 1 << x2APIC ID[3:0].
  //
  // Note we're not currently respecting the DELMODE field and instead are only
  // delivering to the fist local APIC that is targeted.
  return interrupt_fn_(entry.destination(), vector);
}
