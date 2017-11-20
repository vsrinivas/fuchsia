// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hypervisor/gic_distributor.h>

#include <hypervisor/address.h>
#include <hypervisor/bits.h>
#include <hypervisor/guest.h>

static const uint64_t kMaxInterrupts = 128;
static const uint64_t kGicRevision = 2;

// clang-format off

enum class GicdRegister : uint64_t {
    CTL         = 0x000,
    TYPE        = 0x004,
    ISENABLE0   = 0x100,
    ISENABLE15  = 0x13c,
    ICENABLE0   = 0x180,
    ICENABLE15  = 0x1bc,
    ICPEND0     = 0x280,
    ICPEND15    = 0x2bc,
    ICFG0       = 0xc00,
    ICFG31      = 0xc7c,
    PID2        = 0xfe8,
};

// clang-format on

static inline uint32_t typer_it_lines(uint32_t num_interrupts) {
    return set_bits((num_interrupts >> 5) - 1, 4, 0);
}

static inline uint32_t pidr2_arch_rev(uint32_t revision) {
    return set_bits(revision, 7, 4);
}

zx_status_t GicDistributor::Init(Guest* guest) {
    return guest->CreateMapping(TrapType::MMIO_SYNC, GIC_DISTRIBUTOR_PHYS_BASE,
                                GIC_DISTRIBUTOR_SIZE, 0, this);
}

zx_status_t GicDistributor::Read(uint64_t addr, IoValue* value) const {
    if (value->access_size != 4)
        return ZX_ERR_IO_DATA_INTEGRITY;

    switch (static_cast<GicdRegister>(addr)) {
    case GicdRegister::TYPE:
        // TODO(abdulla): Set the number of VCPUs.
        value->u32 = typer_it_lines(kMaxInterrupts);
        return ZX_OK;
    case GicdRegister::ICFG0... GicdRegister::ICFG31:
        if (addr % 4)
            return ZX_ERR_IO_DATA_INTEGRITY;
        value->u32 = 0;
        return ZX_OK;
    case GicdRegister::PID2:
        value->u32 = pidr2_arch_rev(kGicRevision);
        return ZX_OK;
    default:
        fprintf(stderr, "Unhandled GIC distributor address %#lx\n", addr);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t GicDistributor::Write(uint64_t addr, const IoValue& value) {
    switch (static_cast<GicdRegister>(addr)) {
    case GicdRegister::CTL:
        return ZX_OK;
    case GicdRegister::ISENABLE0... GicdRegister::ISENABLE15:
    case GicdRegister::ICENABLE0... GicdRegister::ICENABLE15:
    case GicdRegister::ICPEND0... GicdRegister::ICPEND15:
    case GicdRegister::ICFG0... GicdRegister::ICFG31:
        if (addr % 4)
            return ZX_ERR_IO_DATA_INTEGRITY;
        return ZX_OK;
    default:
        fprintf(stderr, "Unhandled GIC distributor address %#lx\n", addr);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t GicDistributor::Interrupt(uint32_t global_irq) const {
    return ZX_ERR_NOT_SUPPORTED;
}
