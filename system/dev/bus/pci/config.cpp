// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "config.h"
#include "common.h"

#include <assert.h>
#include <ddk/debug.h>
#include <fbl/alloc_checker.h>
#include <inttypes.h>
#include <pretty/hexdump.h>

namespace pci {

// Storage for register constexprs
constexpr PciReg16 Config::kVendorId;
constexpr PciReg16 Config::kDeviceId;
constexpr PciReg16 Config::kCommand;
constexpr PciReg16 Config::kStatus;
constexpr PciReg8 Config::kRevisionId;
constexpr PciReg8 Config::kProgramInterface;
constexpr PciReg8 Config::kSubClass;
constexpr PciReg8 Config::kBaseClass;
constexpr PciReg8 Config::kCacheLineSize;
constexpr PciReg8 Config::kLatencyTimer;
constexpr PciReg8 Config::kHeaderType;
constexpr PciReg8 Config::kBist;
constexpr PciReg32 Config::kCardbusCisPtr;
constexpr PciReg16 Config::kSubsystemVendorId;
constexpr PciReg16 Config::kSubsystemId;
constexpr PciReg32 Config::kExpansionRomAddress;
constexpr PciReg8 Config::kCapabilitiesPtr;
constexpr PciReg8 Config::kInterruptLine;
constexpr PciReg8 Config::kInterruptPin;
constexpr PciReg8 Config::kMinGrant;
constexpr PciReg8 Config::kMaxLatency;
constexpr PciReg8 Config::kPrimaryBusId;
constexpr PciReg8 Config::kSecondaryBusId;
constexpr PciReg8 Config::kSubordinateBusId;
constexpr PciReg8 Config::kSecondaryLatencyTimer;
constexpr PciReg8 Config::kIoBase;
constexpr PciReg8 Config::kIoLimit;
constexpr PciReg16 Config::kSecondaryStatus;
constexpr PciReg16 Config::kMemoryBase;
constexpr PciReg16 Config::kMemoryLimit;
constexpr PciReg16 Config::kPrefetchableMemoryBase;
constexpr PciReg16 Config::kPrefetchableMemoryLimit;
constexpr PciReg32 Config::kPrefetchableMemoryBaseUpper;
constexpr PciReg32 Config::kPrefetchableMemoryLimitUpper;
constexpr PciReg16 Config::kIoBaseUpper;
constexpr PciReg16 Config::kIoLimitUpper;
constexpr PciReg32 Config::kBridgeExpansionRomAddress;
constexpr PciReg16 Config::kBridgeControl;

// MMIO Config Implementation
fbl::RefPtr<Config> MmioConfig::Create(pci_bdf_t bdf, void* ecam_base) {
    ZX_DEBUG_ASSERT(bdf.device_id < PCI_MAX_DEVICES_PER_BUS);
    ZX_DEBUG_ASSERT(bdf.function_id < PCI_MAX_FUNCTIONS_PER_DEVICE);

    // Find the offset into the ecam region for the given bdf address. Every bus
    // has 32 devices, every device has 8 functions, and each function has an
    // extended config space of 4096 bytes.
    zx_paddr_t bdf_start = reinterpret_cast<zx_paddr_t>(ecam_base);
    bdf_start += bdf.bus_id * PCIE_ECAM_BYTES_PER_BUS;
    bdf_start += bdf.device_id * PCI_MAX_FUNCTIONS_PER_DEVICE * PCIE_EXTENDED_CONFIG_SIZE;
    bdf_start += bdf.function_id * PCIE_EXTENDED_CONFIG_SIZE;

    pci_infof("created mmio cfg for bdf %02x:%02x.%1x (base: %#" PRIxPTR ")\n",
              bdf.bus_id, bdf.device_id, bdf.function_id, bdf_start);
    fbl::AllocChecker ac;
    fbl::RefPtr<Config> cfg = AdoptRef(new (&ac) MmioConfig(bdf, bdf_start));
    if (!ac.check()) {
        zxlogf(ERROR, "failed to allocate memory for PciConfig!\n");
    }

    return cfg;
}

uint8_t MmioConfig::Read(const PciReg8 addr) const {
    auto reg = reinterpret_cast<const volatile uint8_t*>(base_ + addr.offset());
    return *reg;
}

uint16_t MmioConfig::Read(const PciReg16 addr) const {
    auto reg = reinterpret_cast<const volatile uint16_t*>(base_ + addr.offset());
    return htole16(*reg);
}

uint32_t MmioConfig::Read(const PciReg32 addr) const {
    auto reg = reinterpret_cast<const volatile uint32_t*>(base_ + addr.offset());
    return htole32(*reg);
}

void MmioConfig::Write(PciReg8 addr, uint8_t val) const {
    auto reg = reinterpret_cast<volatile uint8_t*>(base_ + addr.offset());
    *reg = val;
}

void MmioConfig::Write(PciReg16 addr, uint16_t val) const {
    auto reg = reinterpret_cast<volatile uint16_t*>(base_ + addr.offset());
    *reg = htole16(val);
}

void MmioConfig::Write(PciReg32 addr, uint32_t val) const {
    auto reg = reinterpret_cast<volatile uint32_t*>(base_ + addr.offset());
    *reg = htole32(val);
}

const char* MmioConfig::type(void) const {
    return "mmio";
}

// Proxy Config Implementation
fbl::RefPtr<Config> ProxyConfig::Create(pci_bdf_t bdf, pciroot_protocol_t* proto) {
    fbl::AllocChecker ac;
    fbl::RefPtr<Config> cfg = AdoptRef(new (&ac) ProxyConfig(bdf, proto));
    if (!ac.check()) {
        zxlogf(ERROR, "failed to allocate memory for PciConfig!\n");
    }
    return cfg;
}

uint8_t ProxyConfig::Read(const PciReg8 addr) const {
    uint8_t tmp;
    ZX_DEBUG_ASSERT(ops_);
    ZX_ASSERT(ops_->config_read8(ctx_, &bdf_, addr.offset(), &tmp) == ZX_OK);
    return tmp;
}

uint16_t ProxyConfig::Read(const PciReg16 addr) const {
    uint16_t tmp;
    ZX_DEBUG_ASSERT(ops_);
    ZX_ASSERT(ops_->config_read16(ctx_, &bdf_, addr.offset(), &tmp) == ZX_OK);
    return tmp;
}

uint32_t ProxyConfig::Read(const PciReg32 addr) const {
    uint32_t tmp;
    ZX_DEBUG_ASSERT(ops_);
    ZX_ASSERT(ops_->config_read32(ctx_, &bdf_, addr.offset(), &tmp) == ZX_OK);
    return tmp;
}

void ProxyConfig::Write(PciReg8 addr, uint8_t val) const {
    ZX_DEBUG_ASSERT(ops_);
    ZX_ASSERT(ops_->config_write8(ctx_, &bdf_, addr.offset(), val) == ZX_OK);
}

void ProxyConfig::Write(PciReg16 addr, uint16_t val) const {
    ZX_DEBUG_ASSERT(ops_);
    ZX_ASSERT(ops_->config_write16(ctx_, &bdf_, addr.offset(), val) == ZX_OK);
}

void ProxyConfig::Write(PciReg32 addr, uint32_t val) const {
    ZX_DEBUG_ASSERT(ops_);
    ZX_ASSERT(ops_->config_write32(ctx_, &bdf_, addr.offset(), val) == ZX_OK);
}

const char* ProxyConfig::type(void) const {
    return "proxy";
}

void Config::DumpConfig(uint16_t len) const {
    printf("%u bytes of raw config (type: %s)\n", len, type());
    // PIO space can't be dumped directly so we read a row at a time
    constexpr uint8_t row_len = 16;
    uint32_t pos = 0;
    uint8_t buf[row_len];

    do {
        for (uint16_t i = 0; i < row_len; i++) {
            buf[i] = Read(PciReg8(static_cast<uint8_t>(pos + i)));
        }

        hexdump8_ex(buf, row_len, pos);
        pos += row_len;
    } while (pos < PCI_BASE_CONFIG_SIZE);
}

} // namespace pci
