// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <dev/pci_config.h>

#include <assert.h>
#include <inttypes.h>
#include <trace.h>

#include <mxalloc/new.h>

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
    PANIC_UNIMPLEMENTED;
}
uint16_t PciPioConfig::Read(const PciReg16 addr) const {
    PANIC_UNIMPLEMENTED;
}
uint32_t PciPioConfig::Read(const PciReg32 addr) const {
    PANIC_UNIMPLEMENTED;
}
void PciPioConfig::Write(const PciReg8 addr, uint8_t val) const {
    PANIC_UNIMPLEMENTED;
}
void PciPioConfig::Write(const PciReg16 addr, uint16_t val) const {
    PANIC_UNIMPLEMENTED;
}
void PciPioConfig::Write(const PciReg32 addr, uint32_t val) const {
    PANIC_UNIMPLEMENTED;
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

mxtl::RefPtr<PciConfig> PciConfig::Create(uintptr_t base, PciAddrSpace addr_type) {
    AllocChecker ac;
    mxtl::RefPtr<PciConfig> cfg;

    LTRACEF("base %#" PRIxPTR ", type %s\n", base, (addr_type == PciAddrSpace::PIO) ? "PIO" : "MIO");

    if (addr_type == PciAddrSpace::PIO)
        PANIC_UNIMPLEMENTED;
    else
        cfg = mxtl::AdoptRef(new (&ac) PciMmioConfig(base));

    if (!ac.check()) {
        TRACEF("failed to allocate memory for PciConfig!\n");
        return nullptr;
    }

    return cfg;
}
