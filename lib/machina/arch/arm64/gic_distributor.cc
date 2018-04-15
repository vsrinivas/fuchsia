// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/arch/arm64/gic_distributor.h"

#include <fbl/auto_lock.h>

#include "garnet/lib/machina/address.h"
#include "garnet/lib/machina/bits.h"
#include "garnet/lib/machina/guest.h"
#include "garnet/lib/machina/vcpu.h"
#include "lib/fxl/logging.h"

namespace machina {

static constexpr uint32_t kGicv2Revision = 2;
static constexpr uint32_t kGicv3Revision = 3;
static constexpr uint32_t kGicdCtlr = 0x7;

// clang-format off

enum GicdRegister : uint64_t {
    CTL           = 0x000,
    TYPE          = 0x004,
    IGROUPR0      = 0x080,
    IGROUPR31     = 0x0FC,
    ISENABLE0     = 0x100,
    ISENABLE31    = 0x11c,
    ICENABLE0     = 0x180,
    ICENABLE31    = 0x19c,
    ICPEND0       = 0x280,
    ICPEND31      = 0x2bc,
    ICFG0         = 0xc00,
    ICFG1         = 0xc04,
    ICFG31        = 0xc7c,
    ICACTIVE0     = 0x380,
    ICACTIVE15    = 0x3bc,
    IPRIORITY0    = 0x400,
    IPRIORITY255  = 0x4fc,
    ITARGETS0     = 0x800,
    ITARGETS7     = 0x81c,
    ITARGETS8     = 0x820,
    ITARGETS63    = 0x8fc,
    IGRPMODR0     = 0xd00,
    IGRPMODR31    = 0xd7c,
    SGI           = 0xf00,
    PID2_v2       = 0xfe8,
    // This is the offset of PID2 register when are running GICv3,
    // since the offset mappings of GICD & GICR are 0x1000 apart
    PID2_v2_v3    = 0x1fe8,
    PID2_v3       = 0xffe8,
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

static inline uint32_t typer_it_lines(uint32_t num_interrupts,
                                      GicVersion version) {
  if (version == GicVersion::V2) {
    return set_bits((num_interrupts >> 5) - 1, 4, 0);
  } else {
    return set_bits((num_interrupts >> 5) - 1, 23, 19);
  }
}

static inline uint32_t typer_cpu_number(uint8_t num_cpus) {
  return set_bits(num_cpus - 1, 7, 5);
}

static inline uint32_t pidr2_arch_rev(uint32_t revision) {
  return set_bits(revision, 7, 4);
}

zx_status_t GicDistributor::Init(Guest* guest, GicVersion version) {
  zx_status_t status;
  gic_version_ = version;

  if (version == GicVersion::V2) {
    status =
        guest->CreateMapping(TrapType::MMIO_SYNC, kGicv2DistributorPhysBase,
                             kGicv2DistributorSize, 0, this);
  } else {
    // Map the distributor
    status =
        guest->CreateMapping(TrapType::MMIO_SYNC, kGicv3DistributorPhysBase,
                             kGicv3DistributorSize, 0, this);
    if (status != ZX_OK) {
      return status;
    }

    // Map the redistributor RD Base
    status =
        guest->CreateMapping(TrapType::MMIO_SYNC, kGicv3ReDistributorPhysBase,
                             kGicv3ReDistributorSize, 0, this);
    if (status != ZX_OK) {
      return status;
    }

    // Also map the redistributor SGI Base
    status = guest->CreateMapping(TrapType::MMIO_SYNC,
                                  kGicv3ReDistributor_SGIPhysBase,
                                  kGicv3ReDistributor_SGISize, 0, this);
  }
  return status;
}

zx_status_t GicDistributor::Read(uint64_t addr, IoValue* value) const {
  if (addr % 4 != 0 || value->access_size != 4) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  switch (addr) {
    case GicdRegister::TYPE:
      value->u32 = typer_it_lines(kNumInterrupts, gic_version_) |
                   typer_cpu_number(num_vcpus_);
      return ZX_OK;
    case GicdRegister::ICFG0:
      // SGIs are RAO/WI.
      value->u32 = UINT32_MAX;
      return ZX_OK;
    case GicdRegister::ICFG1... GicdRegister::ICFG31:
      value->u32 = 0;
      return ZX_OK;
    case GicdRegister::ITARGETS0... GicdRegister::ITARGETS7: {
      // GIC Architecture Spec 4.3.12: Each field of ITARGETS0 to ITARGETS7
      // returns a mask that corresponds only to the current processor.
      uint8_t mask = 1u << Vcpu::GetCurrent()->id();
      value->u32 = mask | mask << 8 | mask << 16 | mask << 24;
      return ZX_OK;
    }
    case GicdRegister::ITARGETS8... GicdRegister::ITARGETS63: {
      fbl::AutoLock lock(&mutex_);
      const uint8_t* cpu_mask = &cpu_masks_[addr - GicdRegister::ITARGETS0];
      // Target registers are read from 4 at a time.
      value->u32 = *reinterpret_cast<const uint32_t*>(cpu_mask);
      return ZX_OK;
    }
    case GicdRegister::PID2_v2_v3:
      value->u32 = pidr2_arch_rev(kGicv3Revision);
      return ZX_OK;
    case GicdRegister::PID2_v2:
      value->u32 = pidr2_arch_rev(kGicv2Revision);
      return ZX_OK;
    case GicdRegister::PID2_v3:
      value->u32 = pidr2_arch_rev(kGicv3Revision);
      return ZX_OK;
    case GicdRegister::CTL:
      value->u32 = kGicdCtlr;
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
    case GicdRegister::ITARGETS0... GicdRegister::ITARGETS7: {
      // GIC Architecture Spec 4.3.12: ITARGETS0 to ITARGETS7 are read only.
      FXL_LOG(ERROR) << "Write to read-only GIC distributor address 0x"
                     << std::hex << addr;
      return ZX_ERR_INVALID_ARGS;
    }
    case GicdRegister::ITARGETS8... GicdRegister::ITARGETS63: {
      fbl::AutoLock lock(&mutex_);
      uint8_t* cpu_mask = &cpu_masks_[addr - GicdRegister::ITARGETS0];
      *reinterpret_cast<uint32_t*>(cpu_mask) = value.u32;
      return ZX_OK;
    }
    case GicdRegister::SGI: {
      SoftwareGeneratedInterrupt sgi(value.u32);
      uint8_t cpu_mask;
      switch (sgi.target) {
        case InterruptTarget::MASK:
          cpu_mask = sgi.cpu_mask;
          break;
        case InterruptTarget::ALL_BUT_LOCAL:
          cpu_mask = ~(1u << Vcpu::GetCurrent()->id());
          break;
        case InterruptTarget::LOCAL:
          cpu_mask = 1u << Vcpu::GetCurrent()->id();
          break;
        default:
          return ZX_ERR_NOT_SUPPORTED;
      }
      return TargetInterrupt(sgi.vector, cpu_mask);
    }
    case GicdRegister::ISENABLE0... GicdRegister::ISENABLE31: {
      fbl::AutoLock lock(&mutex_);
      uint8_t* enable = &enabled_[addr - GicdRegister::ISENABLE0];
      *reinterpret_cast<uint32_t*>(enable) |= value.u32;
      return ZX_OK;
    }
    case GicdRegister::ICENABLE0... GicdRegister::ICENABLE31: {
      fbl::AutoLock lock(&mutex_);
      uint8_t* enable = &enabled_[addr - GicdRegister::ICENABLE0];
      *reinterpret_cast<uint32_t*>(enable) &= ~value.u32;
      return ZX_OK;
    }
    case GicdRegister::CTL:
    case GicdRegister::ICACTIVE0... GicdRegister::ICACTIVE15:
    case GicdRegister::ICFG0... GicdRegister::ICFG31:
    case GicdRegister::ICPEND0... GicdRegister::ICPEND31:
    case GicdRegister::IPRIORITY0... GicdRegister::IPRIORITY255:
    case GicdRegister::IGROUPR0... GicdRegister::IGROUPR31:
    case GicdRegister::IGRPMODR0... GicdRegister::IGRPMODR31:
      return ZX_OK;
    default:
      FXL_LOG(ERROR) << "Unhandled GIC distributor address write 0x" << std::hex
                     << addr;
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t GicDistributor::RegisterVcpu(uint8_t vcpu_num, Vcpu* vcpu) {
  if (vcpu_num > kMaxVcpus) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (vcpus_[vcpu_num] != nullptr) {
    return ZX_ERR_ALREADY_EXISTS;
  }
  vcpus_[vcpu_num] = vcpu;
  num_vcpus_ += 1;
  // We set the default state of all CPU masks to target every registered VCPU.
  uint8_t default_mask = cpu_masks_[0] | 1u << vcpu_num;
  memset(cpu_masks_, default_mask, sizeof(cpu_masks_));
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

zx_status_t GicDistributor::TargetInterrupt(uint32_t global_irq,
                                            uint8_t cpu_mask) {
  if (global_irq >= kNumInterrupts) {
    return ZX_ERR_INVALID_ARGS;
  }
  {
    fbl::AutoLock lock(&mutex_);
    bool is_enabled =
        enabled_[global_irq / CHAR_BIT] & (1u << global_irq % CHAR_BIT);
    if (!is_enabled) {
      return ZX_OK;
    }
  }
  for (size_t i = 0; cpu_mask != 0; cpu_mask >>= 1, i++) {
    if (!(cpu_mask & 1) || vcpus_[i] == nullptr) {
      continue;
    }
    zx_status_t status = vcpus_[i]->Interrupt(global_irq);
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

}  // namespace machina
