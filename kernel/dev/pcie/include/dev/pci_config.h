// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <debug.h>
#include <endian.h>
#include <fbl/ref_ptr.h>
#include <fbl/ref_counted.h>
#include <fbl/intrusive_single_list.h>
#include <dev/pci_common.h>

class PciReg8 {
public:
    constexpr explicit PciReg8(uint16_t offset)
        : offset_(offset) {}
    constexpr PciReg8()
        : offset_(0u) {}
    constexpr uint16_t offset() const { return offset_; }

private:
    uint16_t offset_;
};

class PciReg16 {
public:
    constexpr explicit PciReg16(uint16_t offset)
        : offset_(offset) {};
    constexpr PciReg16()
        : offset_(0u) {}
    constexpr uint16_t offset() const { return offset_; }

private:
    uint16_t offset_;
};

class PciReg32 {
public:
    constexpr explicit PciReg32(uint16_t offset)
        : offset_(offset) {};
    constexpr PciReg32()
        : offset_(0u) {}
    constexpr uint16_t offset() const { return offset_; }

private:
    uint16_t offset_;
};

/* PciConfig supplies the factory for creating the appropriate pci config
 * object based on the address space of the pci device. */
class PciConfig : public fbl::SinglyLinkedListable<fbl::RefPtr<PciConfig>>
                , public fbl::RefCounted<PciConfig> {
public:
    // Standard PCI configuration space values. Offsets from PCI Firmware Spec ch 6.
    static constexpr PciReg16 kVendorId = PciReg16(0x0);
    static constexpr PciReg16 kDeviceId = PciReg16(0x2);
    static constexpr PciReg16 kCommand = PciReg16(0x4);
    static constexpr PciReg16 kStatus = PciReg16(0x6);
    static constexpr PciReg8 kRevisionId = PciReg8(0x8);
    static constexpr PciReg8 kProgramInterface = PciReg8(0x9);
    static constexpr PciReg8 kSubClass = PciReg8(0xA);
    static constexpr PciReg8 kBaseClass = PciReg8(0xB);
    static constexpr PciReg8 kCacheLineSize = PciReg8(0xC);
    static constexpr PciReg8 kLatencyTimer = PciReg8(0xD);
    static constexpr PciReg8 kHeaderType = PciReg8(0xE);
    static constexpr PciReg8 kBist = PciReg8(0xF);
    /* 0x10 is the address of the first BAR in config space
     * BAR rather than BaseAddress for space / sanity considerations */
    static constexpr PciReg32 kBAR(uint bar) {
        DEBUG_ASSERT(bar < PCIE_MAX_BAR_REGS);
        return PciReg32(static_cast<uint16_t>(0x10 + (bar * sizeof(uint32_t))));
    }
    static constexpr PciReg32 kCardbusCisPtr = PciReg32(0x28);
    static constexpr PciReg16 kSubsystemVendorId = PciReg16(0x2C);
    static constexpr PciReg16 kSubsystemId = PciReg16(0x2E);
    static constexpr PciReg32 kExpansionRomAddress = PciReg32(0x30);
    static constexpr PciReg8 kCapabilitiesPtr = PciReg8(0x34);
    // 0x35 through 0x3B is reserved
    static constexpr PciReg8 kInterruptLine = PciReg8(0x3C);
    static constexpr PciReg8 kInterruptPin = PciReg8(0x3D);
    static constexpr PciReg8 kMinGrant = PciReg8(0x3E);
    static constexpr PciReg8 kMaxLatency = PciReg8(0x3F);
    static constexpr uint8_t kStdCfgEnd = static_cast<uint8_t>(kMaxLatency.offset() + sizeof(uint8_t));

    /* pci to pci bridge config
     * Unlike a normal PCI header, a bridge only has two BARs, but the BAR offset in config space
     * is the same. */
    static constexpr PciReg8 kPrimaryBusId = PciReg8(0x18);
    static constexpr PciReg8 kSecondaryBusId = PciReg8(0x19);
    static constexpr PciReg8 kSubordinateBusId = PciReg8(0x1A);
    static constexpr PciReg8 kSecondaryLatencyTimer = PciReg8(0x1B);
    static constexpr PciReg8 kIoBase = PciReg8(0x1C);
    static constexpr PciReg8 kIoLimit = PciReg8(0x1D);
    static constexpr PciReg16 kSecondaryStatus = PciReg16(0x1E);
    static constexpr PciReg16 kMemoryBase = PciReg16(0x20);
    static constexpr PciReg16 kMemoryLimit = PciReg16(0x22);
    static constexpr PciReg16 kPrefetchableMemoryBase = PciReg16(0x24);
    static constexpr PciReg16 kPrefetchableMemoryLimit = PciReg16(0x26);
    static constexpr PciReg32 kPrefetchableMemoryBaseUpper = PciReg32(0x28);
    static constexpr PciReg32 kPrefetchableMemoryLimitUpper = PciReg32(0x2C);
    static constexpr PciReg16 kIoBaseUpper = PciReg16(0x30);
    static constexpr PciReg16 kIoLimitUpper = PciReg16(0x32);
    // Capabilities Pointer for a bridge matches the standard 0x34 offset
    // 0x35 through 0x38 is reserved
    static constexpr PciReg32 kBridgeExpansionRomAddress = PciReg32(0x38);
    // interrupt line for a bridge matches the standard 0x3C offset
    // interrupt pin for a bridge matches the standard 0x3D offset
    static constexpr PciReg16 kBridgeControl = PciReg16(0x3E);

    /** Create a Pci Configuration object of the appropriate type.
     *
     * @param base The base address for the PCI configuration space.  @param
     * @param addr_type An enum value of PciAddrSpace to identify the time of address
     * space the configuration object will use.
     *
     * @return a pointer to a new PciConfig instance on success, nullptr on failure.
     */
    static fbl::RefPtr<PciConfig> Create(uintptr_t base, PciAddrSpace addr_type);
    inline uintptr_t base() const { return base_; }
    inline PciAddrSpace addr_space() const { return addr_space_; }

    // Virtuals
    void DumpConfig(uint16_t len) const;
    virtual uint8_t Read(const PciReg8 addr) const = 0;
    virtual uint16_t Read(const PciReg16 addr) const = 0;
    virtual uint32_t Read(const PciReg32 addr) const = 0;
    virtual void Write(const PciReg8 addr, uint8_t val) const = 0;
    virtual void Write(const PciReg16 addr, uint16_t val) const = 0;
    virtual void Write(const PciReg32 addr, uint32_t val) const = 0;
    virtual ~PciConfig(){};

protected:
    PciConfig(uintptr_t base, PciAddrSpace addr_space)
        : addr_space_(addr_space), base_(base) {}
    const PciAddrSpace addr_space_;
    const uintptr_t base_;
};
