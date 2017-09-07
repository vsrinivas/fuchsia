// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <dev/pci_config.h>
#include <lib/pci/pio.h>

#include <assert.h>
#include <debug.h>
#include <inttypes.h>
#include <trace.h>

#include <fbl/alloc_checker.h>

#define LOCAL_TRACE 0

// Storage for register constexprs
constexpr PciReg16 PciConfig::kVendorId;
constexpr PciReg16 PciConfig::kDeviceId;
constexpr PciReg16 PciConfig::kCommand;
constexpr PciReg16 PciConfig::kStatus;
constexpr PciReg8 PciConfig::kRevisionId;
constexpr PciReg8 PciConfig::kProgramInterface;
constexpr PciReg8 PciConfig::kSubClass;
constexpr PciReg8 PciConfig::kBaseClass;
constexpr PciReg8 PciConfig::kCacheLineSize;
constexpr PciReg8 PciConfig::kLatencyTimer;
constexpr PciReg8 PciConfig::kHeaderType;
constexpr PciReg8 PciConfig::kBist;
constexpr PciReg32 PciConfig::kCardbusCisPtr;
constexpr PciReg16 PciConfig::kSubsystemVendorId;
constexpr PciReg16 PciConfig::kSubsystemId;
constexpr PciReg32 PciConfig::kExpansionRomAddress;
constexpr PciReg8 PciConfig::kCapabilitiesPtr;
constexpr PciReg8 PciConfig::kInterruptLine;
constexpr PciReg8 PciConfig::kInterruptPin;
constexpr PciReg8 PciConfig::kMinGrant;
constexpr PciReg8 PciConfig::kMaxLatency;
constexpr PciReg8 PciConfig::kPrimaryBusId;
constexpr PciReg8 PciConfig::kSecondaryBusId;
constexpr PciReg8 PciConfig::kSubordinateBusId;
constexpr PciReg8 PciConfig::kSecondaryLatencyTimer;
constexpr PciReg8 PciConfig::kIoBase;
constexpr PciReg8 PciConfig::kIoLimit;
constexpr PciReg16 PciConfig::kSecondaryStatus;
constexpr PciReg16 PciConfig::kMemoryBase;
constexpr PciReg16 PciConfig::kMemoryLimit;
constexpr PciReg16 PciConfig::kPrefetchableMemoryBase;
constexpr PciReg16 PciConfig::kPrefetchableMemoryLimit;
constexpr PciReg32 PciConfig::kPrefetchableMemoryBaseUpper;
constexpr PciReg32 PciConfig::kPrefetchableMemoryLimitUpper;
constexpr PciReg16 PciConfig::kIoBaseUpper;
constexpr PciReg16 PciConfig::kIoLimitUpper;
constexpr PciReg32 PciConfig::kBridgeExpansionRomAddress;
constexpr PciReg16 PciConfig::kBridgeControl;

/*
 * Derived classes should not be in the global namespace, all users
 * of PciConfig should only have the base interface available
 */
namespace {

class PciPioConfig : public PciConfig {
public:
    PciPioConfig(uintptr_t base)
        : PciConfig(base, PciAddrSpace::PIO) {}
    uint8_t Read(const PciReg8 addr) const override;
    uint16_t Read(const PciReg16 addr) const override;
    uint32_t Read(const PciReg32 addr) const override;
    void Write(const PciReg8 addr, uint8_t val) const override;
    void Write(const PciReg16 addr, uint16_t val) const override;
    void Write(const PciReg32 addr, uint32_t val) const override;

private:
    friend PciConfig;
};

uint8_t PciPioConfig::Read(const PciReg8 addr) const {
    uint32_t val;
    mx_status_t status = Pci::PioCfgRead(static_cast<uint32_t>(base_ + addr.offset()), &val, 8u);
    DEBUG_ASSERT(status == MX_OK);
    return static_cast<uint8_t>(val & 0xFF);
}
uint16_t PciPioConfig::Read(const PciReg16 addr) const {
    uint32_t val;
    mx_status_t status = Pci::PioCfgRead(static_cast<uint32_t>(base_ + addr.offset()), &val, 16u);
    DEBUG_ASSERT(status == MX_OK);
    return static_cast<uint16_t>(val & 0xFFFF);
}
uint32_t PciPioConfig::Read(const PciReg32 addr) const {
    uint32_t val;
    mx_status_t status = Pci::PioCfgRead(static_cast<uint32_t>(base_ + addr.offset()), &val, 32u);
    DEBUG_ASSERT(status == MX_OK);
    return val;
}
void PciPioConfig::Write(const PciReg8 addr, uint8_t val) const {
    mx_status_t status = Pci::PioCfgWrite(static_cast<uint32_t>(base_ + addr.offset()), val, 8u);
    DEBUG_ASSERT(status == MX_OK);
}
void PciPioConfig::Write(const PciReg16 addr, uint16_t val) const {
    mx_status_t status = Pci::PioCfgWrite(static_cast<uint32_t>(base_ + addr.offset()), val, 16u);
    DEBUG_ASSERT(status == MX_OK);
}
void PciPioConfig::Write(const PciReg32 addr, uint32_t val) const {
    mx_status_t status = Pci::PioCfgWrite(static_cast<uint32_t>(base_ + addr.offset()), val, 32u);
    DEBUG_ASSERT(status == MX_OK);
}

class PciMmioConfig : public PciConfig {
public:
    PciMmioConfig(uintptr_t base)
        : PciConfig(base, PciAddrSpace::MMIO) {}
    uint8_t Read(const PciReg8 addr) const override;
    uint16_t Read(const PciReg16 addr) const override;
    uint32_t Read(const PciReg32 addr) const override;
    void Write(const PciReg8 addr, uint8_t val) const override;
    void Write(const PciReg16 addr, uint16_t val) const override;
    void Write(const PciReg32 addr, uint32_t val) const override;

private:
    friend PciConfig;
};

// MMIO Config Implementation
uint8_t PciMmioConfig::Read(const PciReg8 addr) const {
    auto reg = reinterpret_cast<const volatile uint8_t*>(base_ + addr.offset());
    return *reg;
}

uint16_t PciMmioConfig::Read(const PciReg16 addr) const {
    auto reg = reinterpret_cast<const volatile uint16_t*>(base_ + addr.offset());
    return LE16(*reg);
}

uint32_t PciMmioConfig::Read(const PciReg32 addr) const {
    auto reg = reinterpret_cast<const volatile uint32_t*>(base_ + addr.offset());
    return LE32(*reg);
}

void PciMmioConfig::Write(PciReg8 addr, uint8_t val) const {
    auto reg = reinterpret_cast<volatile uint8_t*>(base_ + addr.offset());
    *reg = val;
}

void PciMmioConfig::Write(PciReg16 addr, uint16_t val) const {
    auto reg = reinterpret_cast<volatile uint16_t*>(base_ + addr.offset());
    *reg = LE16(val);
}

void PciMmioConfig::Write(PciReg32 addr, uint32_t val) const {
    auto reg = reinterpret_cast<volatile uint32_t*>(base_ + addr.offset());
    *reg = LE32(val);
}

} // anon namespace

fbl::RefPtr<PciConfig> PciConfig::Create(uintptr_t base, PciAddrSpace addr_type) {
    fbl::AllocChecker ac;
    fbl::RefPtr<PciConfig> cfg = nullptr;

    LTRACEF("base %#" PRIxPTR ", type %s\n", base, (addr_type == PciAddrSpace::PIO) ? "PIO" : "MIO");

    if (addr_type == PciAddrSpace::PIO) {
        cfg = fbl::AdoptRef(new (&ac) PciPioConfig(base));
    } else {
        cfg = fbl::AdoptRef(new (&ac) PciMmioConfig(base));
    }

    if (!ac.check()) {
        TRACEF("failed to allocate memory for PciConfig!\n");
    }

    return cfg;
}

void PciConfig::DumpConfig(uint16_t len) const {
    printf("%u bytes of raw config (base %s:%#" PRIxPTR ")\n",
           len, (addr_space_ == PciAddrSpace::MMIO) ? "MMIO" : "PIO", base_);
    if (addr_space_ == PciAddrSpace::MMIO) {
        hexdump8(reinterpret_cast<const void*>(base_), len);
    } else {
        // PIO space can't be dumped directly so we read a row at a time
        constexpr uint8_t row_len = 16;
        uint32_t pos = 0;
        uint8_t buf[row_len];

        do {
            for (uint16_t i = 0; i < row_len; i++) {
                buf[i] = Read(PciReg8(static_cast<uint8_t>(pos + i)));
            }

            hexdump8_ex(buf, row_len, base_ + pos);
            pos += row_len;
        } while (pos < PCIE_BASE_CONFIG_SIZE);
    }
}
