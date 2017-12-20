// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/arch/arm64/gic_distributor.h"

#include <fbl/auto_lock.h>
#include <hypervisor/bits.h>
#include <hypervisor/guest.h>
#include <hypervisor/vcpu.h>

#include "garnet/lib/machina/address.h"
#include "lib/fxl/logging.h"

namespace machina {

static const uint64_t kGicRevision = 2;

// clang-format off

enum GicdRegister : uint64_t {
    CTL           = 0x000,
    TYPE          = 0x004,
    ISENABLE0     = 0x100,
    ISENABLE15    = 0x13c,
    ICENABLE0     = 0x180,
    ICENABLE15    = 0x1bc,
    ICPEND0       = 0x280,
    ICPEND15      = 0x2bc,
    ICFG0         = 0xc00,
    ICFG1         = 0xc04,
    ICFG31        = 0xc7c,
    ICACTIVE0     = 0x380,
    ICACTIVE15    = 0x3bc,
    IPRIORITY0    = 0x400,
    IPRIORITY255  = 0x4fc,
    ITARGETS0     = 0x800,
    ITARGETS255   = 0x8fc,
    SGI           = 0xf00,
    PID2          = 0xfe8,
};

// Target CPU for the software-generated interrupt.
enum class InterruptTarget {
  MASK            = 0b00,
  ALL_BUT_LOCAL   = 0b01,
  LOCAL           = 0b10,
};

// clang-format on

// Software-generated interrupt received by the GIC distributor.
struct SoftwareGeneratedInterrupt {
  InterruptTarget target;
  uint8_t cpu_mask;
  uint8_t vector;

  SoftwareGeneratedInterrupt(uint32_t sgi) {
    target = static_cast<InterruptTarget>(bits_shift(sgi, 25, 24));
    cpu_mask = static_cast<uint8_t>(bits_shift(sgi, 23, 16));
    vector = static_cast<uint8_t>(bits_shift(sgi, 3, 0));
  }
};

static inline uint32_t typer_it_lines(uint32_t num_interrupts) {
  return set_bits((num_interrupts >> 5) - 1, 4, 0);
}

static inline uint32_t pidr2_arch_rev(uint32_t revision) {
  return set_bits(revision, 7, 4);
}

zx_status_t GicDistributor::Init(Guest* guest) {
  return guest->CreateMapping(TrapType::MMIO_SYNC, kGicDistributorPhysBase,
                              kGicDistributorSize, 0, this);
}

zx_status_t GicDistributor::Read(uint64_t addr, IoValue* value) const {
  if (addr % 4 != 0 || value->access_size != 4) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  switch (addr) {
    case GicdRegister::TYPE:
      // TODO(abdulla): Set the number of VCPUs.
      value->u32 = typer_it_lines(kNumInterrupts);
      return ZX_OK;
    case GicdRegister::ICFG0:
      // SGIs are RAO/WI.
      value->u32 = UINT32_MAX;
      return ZX_OK;
    case GicdRegister::ICFG1... GicdRegister::ICFG31:
      value->u32 = 0;
      return ZX_OK;
    case GicdRegister::ITARGETS0... GicdRegister::ITARGETS255: {
      fbl::AutoLock lock(&mutex_);
      const uint8_t* cpu_mask = &cpu_masks_[addr - GicdRegister::ITARGETS0];
      // Target registers are read from 4 at a time.
      value->u32 = *reinterpret_cast<const uint32_t*>(cpu_mask);
      return ZX_OK;
    }
    case GicdRegister::PID2:
      value->u32 = pidr2_arch_rev(kGicRevision);
      return ZX_OK;
    default:
      FXL_LOG(ERROR) << "Unhandled GIC distributor address read 0x" << std::hex
                     << addr;
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t GicDistributor::Write(uint64_t addr, const IoValue& value) {
  if (addr % 4 != 0 || value.access_size != 4) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  switch (addr) {
    case GicdRegister::ITARGETS0... GicdRegister::ITARGETS255: {
      fbl::AutoLock lock(&mutex_);
      uint8_t* cpu_mask = &cpu_masks_[addr - GicdRegister::ITARGETS0];
      // Target registers are written to 4 at a time.
      *reinterpret_cast<uint32_t*>(cpu_mask) = value.u32;
      return ZX_OK;
    }
    case GicdRegister::SGI: {
      SoftwareGeneratedInterrupt sgi(value.u32);
      if (sgi.target != InterruptTarget::MASK) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      return TargetInterrupt(sgi.vector, sgi.cpu_mask);
    }
    case GicdRegister::CTL:
    case GicdRegister::ICACTIVE0... GicdRegister::ICACTIVE15:
    case GicdRegister::ICENABLE0... GicdRegister::ICENABLE15:
    case GicdRegister::ICFG0... GicdRegister::ICFG31:
    case GicdRegister::ICPEND0... GicdRegister::ICPEND15:
    case GicdRegister::IPRIORITY0... GicdRegister::IPRIORITY255:
    case GicdRegister::ISENABLE0... GicdRegister::ISENABLE15:
      return ZX_OK;
    default:
      FXL_LOG(ERROR) << "Unhandled GIC distributor address write 0x" << std::hex
                     << addr;
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t GicDistributor::RegisterVcpu(uint8_t vcpu_id, Vcpu* vcpu) {
  if (vcpu_id > kMaxVcpus) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (vcpus_[vcpu_id] != nullptr) {
    return ZX_ERR_ALREADY_EXISTS;
  }
  vcpus_[vcpu_id] = vcpu;
  return ZX_OK;
}

zx_status_t GicDistributor::Interrupt(uint32_t global_irq) {
  uint8_t cpu_mask;
  {
    fbl::AutoLock lock(&mutex_);
    cpu_mask = cpu_masks_[global_irq];
  }
  return TargetInterrupt(global_irq, cpu_mask);
}

zx_status_t GicDistributor::TargetInterrupt(uint32_t global_irq, uint8_t cpu_mask) {
  for (Vcpu** vcpu = vcpus_; cpu_mask != 0; vcpu++, cpu_mask >>= 1) {
    if (!(cpu_mask & 1)) {
      continue;
    } else if (vcpu == nullptr) {
      return ZX_ERR_BAD_STATE;
    }
    zx_status_t status = (*vcpu)->Interrupt(global_irq);
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

}  // namespace machina
