// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <assert.h>
#include <endian.h>
#include <kernel/mutex.h>
#include <magenta/types.h>
#include <lib/pci/pio.h>
#include <kernel/auto_lock.h>

// TODO: This library exists as a shim for the awkward period between bringing
// PCI legacy support online, and moving PCI to userspace. Initially, it exists
// as a kernel library that userspace accesses via syscalls so that a userspace
// process never causes a race condition with the bus driver's accesses. Later,
// all accesses will go through the library itself in userspace and the syscalls
// will no longer exist.

namespace Pci {

#ifdef ARCH_X86
#include <arch/x86.h>
static mutex_t pio_lock = MUTEX_INITIAL_VALUE(pio_lock);

static constexpr uint16_t kPciConfigAddr = 0xCF8;
static constexpr uint16_t kPciConfigData = 0xCFC;
static constexpr uint32_t kPciCfgEnable = (1 << 31);
static constexpr uint32_t WidthMask(size_t width) {
    return (width == 32) ? 0xffffffff : (1u << width) - 1u;
}

mx_status_t PioCfgRead(uint32_t addr, uint32_t* val, size_t width) {
    fbl::AutoLock lock(&pio_lock);

    size_t shift = (addr & 0x3) * 8u;
    if (shift + width > 32) {
        return MX_ERR_INVALID_ARGS;
    }

    outpd(kPciConfigAddr, addr | kPciCfgEnable);;
    uint32_t tmp_val = LE32(inpd(kPciConfigData));
    uint32_t width_mask = WidthMask(width);

    // Align the read to the correct offset, then mask based on byte width
    *val = (tmp_val >> shift) & width_mask;
    return MX_OK;
}

mx_status_t PioCfgRead(uint8_t bus, uint8_t dev, uint8_t func,
                             uint8_t offset, uint32_t* val, size_t width) {
    return PioCfgRead(PciBdfAddr(bus, dev, func, offset), val, width);
}

mx_status_t PioCfgWrite(uint32_t addr, uint32_t val, size_t width) {
    fbl::AutoLock lock(&pio_lock);

    size_t shift = (addr & 0x3) * 8u;
    if (shift + width > 32) {
        return MX_ERR_INVALID_ARGS;
    }

    uint32_t width_mask = WidthMask(width);
    uint32_t write_mask = width_mask << shift;
    outpd(kPciConfigAddr, addr | kPciCfgEnable);
    uint32_t tmp_val = LE32(inpd(kPciConfigData));

    val &= width_mask;
    tmp_val &= ~write_mask;
    tmp_val |= (val << shift);
    outpd(kPciConfigData, LE32(tmp_val));

    return MX_OK;
}

mx_status_t PioCfgWrite(uint8_t bus, uint8_t dev, uint8_t func,
                              uint8_t offset, uint32_t val, size_t width) {
    return PioCfgWrite(PciBdfAddr(bus, dev, func, offset), val, width);
}

#else // not x86
mx_status_t PioCfgRead(uint32_t addr, uint32_t* val, size_t width) {
    return MX_ERR_NOT_SUPPORTED;
}

mx_status_t PioCfgRead(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset,
                             uint32_t* val, size_t width) {
    return MX_ERR_NOT_SUPPORTED;
}

mx_status_t PioCfgWrite(uint32_t addr, uint32_t val, size_t width) {
    return MX_ERR_NOT_SUPPORTED;;
}

mx_status_t PioCfgWrite(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset,
                             uint32_t val, size_t width) {
    return MX_ERR_NOT_SUPPORTED;
}

#endif // ARCH_X86
}; // namespace PCI
