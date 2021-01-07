// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_DRIVERS_PCI_CONFIG_H_
#define SRC_DEVICES_BUS_DRIVERS_PCI_CONFIG_H_

#include <endian.h>
#include <fuchsia/hardware/pciroot/cpp/banjo.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/status.h>
#include <stdio.h>
#include <zircon/errors.h>
#include <zircon/hw/pci.h>
#include <zircon/types.h>

#include <ddk/mmio-buffer.h>
#include <hwreg/bitfields.h>

namespace pci {
namespace config {

// Fields correspond to the name in the PCI Local Bus Spec section 6.2.
struct Command {
  uint16_t value;
  // 15-11 are reserved preserve.
  DEF_SUBBIT(value, 10, interrupt_disable);
  DEF_SUBBIT(value, 9, fast_back_to_back_enable);
  DEF_SUBBIT(value, 8, serr_enable);
  // 7 is reserved preserve.
  DEF_SUBBIT(value, 6, parity_error_response);
  DEF_SUBBIT(value, 5, vga_palette_snoop);
  DEF_SUBBIT(value, 4, memory_write_and_invalidate_enable);
  DEF_SUBBIT(value, 3, special_cycles);
  DEF_SUBBIT(value, 2, bus_master);
  DEF_SUBBIT(value, 1, memory_space);
  DEF_SUBBIT(value, 0, io_space);
};

// The layout of a Base Address Register changes based on its type.
// PCI Local Bus Spec section 6.2.5.1.
struct IoBaseAddress;
struct MmioBaseAddress;
struct BaseAddress : public hwreg::RegisterBase<BaseAddress, uint32_t> {
  DEF_RSVDZ_BIT(1);
  DEF_BIT(0, is_io_space);

  IoBaseAddress* io() { return reinterpret_cast<IoBaseAddress*>(this); }
  MmioBaseAddress* mmio() { return reinterpret_cast<MmioBaseAddress*>(this); }
  static auto Get() { return hwreg::RegisterAddr<BaseAddress>(0); }
};

struct IoBaseAddress : public BaseAddress {
  DEF_UNSHIFTED_FIELD(31, 2, base_address);
};

struct MmioBaseAddress : public BaseAddress {
  DEF_UNSHIFTED_FIELD(31, 4, base_address);
  DEF_BIT(3, is_prefetchable);
  DEF_BIT(2, is_64_bit);
};

}  // namespace config

constexpr zx_paddr_t bdf_to_ecam_offset(pci_bdf_t bdf, uint8_t start_bus) {
  // Find the offset into the ecam region for the given bdf address. Every bus
  // has 32 devices, every device has 8 functions, and each function has an
  // extended config space of 4096 bytes. The base address of the vmo provided
  // to the bus driver corresponds to the start_bus_num, so offset the bdf address
  // based on the bottom of our ecam.
  zx_vaddr_t bdf_start = (bdf.bus_id - start_bus) * PCIE_ECAM_BYTES_PER_BUS;
  bdf_start += bdf.device_id * PCI_MAX_FUNCTIONS_PER_DEVICE * PCIE_EXTENDED_CONFIG_SIZE;
  bdf_start += bdf.function_id * PCIE_EXTENDED_CONFIG_SIZE;
  return bdf_start;
}

class PciReg8 {
 public:
  constexpr explicit PciReg8(uint16_t offset) : offset_(offset) {}
  constexpr PciReg8() : offset_(0u) {}
  constexpr uint16_t offset() const { return offset_; }
  inline bool operator==(const PciReg8& other) { return (offset() == other.offset()); }

 private:
  uint16_t offset_;
};

class PciReg16 {
 public:
  constexpr explicit PciReg16(uint16_t offset) : offset_(offset) {}
  constexpr PciReg16() : offset_(0u) {}
  constexpr uint16_t offset() const { return offset_; }
  inline bool operator==(const PciReg8& other) { return (offset() == other.offset()); }

 private:
  uint16_t offset_;
};

class PciReg32 {
 public:
  constexpr explicit PciReg32(uint16_t offset) : offset_(offset) {}
  constexpr PciReg32() : offset_(0u) {}
  constexpr uint16_t offset() const { return offset_; }
  inline bool operator==(const PciReg8& other) { return (offset() == other.offset()); }

 private:
  uint16_t offset_;
};

// Config supplies the factory for creating the appropriate pci config
// object based on the address space of the pci device.
class Config {
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
  // 0x10 is the address of the first BAR in config space
  // BAR rather than BaseAddress for space / sanity considerations
  static constexpr PciReg32 kBar(uint32_t bar) {
    ZX_ASSERT(bar < PCI_MAX_BAR_REGS);
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
  static constexpr uint8_t kStdCfgEnd =
      static_cast<uint8_t>(kMaxLatency.offset() + sizeof(uint8_t));

  // pci to pci bridge config
  // Unlike a normal PCI header, a bridge only has two BARs, but the BAR offset in config space
  // is the same.
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

  inline const pci_bdf_t& bdf() const { return bdf_; }
  inline const char* addr(void) const { return addr_; }
  virtual const char* type(void) const = 0;
  // Return a copy of the MmioView backing the Config's MMIO space, if supported.
  virtual zx::status<ddk::MmioView> get_view() const { return zx::error(ZX_ERR_NOT_SUPPORTED); }

  // Virtuals
  void DumpConfig(uint16_t len) const;
  virtual uint8_t Read(const PciReg8 addr) const = 0;
  virtual uint16_t Read(const PciReg16 addr) const = 0;
  virtual uint32_t Read(const PciReg32 addr) const = 0;
  virtual void Write(const PciReg8 addr, uint8_t val) const = 0;
  virtual void Write(const PciReg16 addr, uint16_t val) const = 0;
  virtual void Write(const PciReg32 addr, uint32_t val) const = 0;
  virtual ~Config() {}

 protected:
  Config(pci_bdf_t bdf) : bdf_(bdf) {
    snprintf(addr_, sizeof(addr_), "%02x:%02x.%01x", bdf_.bus_id, bdf_.device_id, bdf_.function_id);
  }
  const pci_bdf_t bdf_;
  char addr_[8];
};

// MMIO config is the stardard method for accessing modern pci configuration space.
// A device's configuration space is mapped to a specific place in a given pci root's
// ecam and can be directly accessed with standard IO operations.t
class MmioConfig : public Config {
 public:
  static zx_status_t Create(pci_bdf_t bdf, ddk::MmioBuffer* ecam_, uint8_t start_bus,
                            uint8_t end_bus, std::unique_ptr<Config>* config);
  uint8_t Read(const PciReg8 addr) const final;
  uint16_t Read(const PciReg16 addr) const final;
  uint32_t Read(const PciReg32 addr) const final;
  void Write(const PciReg8 addr, uint8_t val) const final;
  void Write(const PciReg16 addr, uint16_t val) const final;
  void Write(const PciReg32 addr, uint32_t val) const override;
  const char* type(void) const override;
  virtual zx::status<ddk::MmioView> get_view() const final { return zx::ok(ddk::MmioView(view_)); }

 private:
  friend class FakeMmioConfig;
  MmioConfig(pci_bdf_t bdf, ddk::MmioView&& view) : Config(bdf), view_(std::move(view)) {}
  const ddk::MmioView view_;
};

// ProxyConfig is used with PCI buses that do not support MMIO config space,
// or require special controller configuration before config access. Examples
// of this are IO config on x64 due to needing to synchronize CF8/CFC with
// ACPI, and Designware on ARM where the controller needs to be configured to
// map a given device's configuration space in before access.
//
// For proxy configuration access all operations are passed to the pciroot
// protocol implementation hosted in the same devhost as the pci bus driver.
class ProxyConfig final : public Config {
 public:
  static zx_status_t Create(pci_bdf_t bdf, ddk::PcirootProtocolClient* proto,
                            std::unique_ptr<Config>* config);
  uint8_t Read(const PciReg8 addr) const final;
  uint16_t Read(const PciReg16 addr) const final;
  uint32_t Read(const PciReg32 addr) const final;
  void Write(const PciReg8 addr, uint8_t val) const final;
  void Write(const PciReg16 addr, uint16_t val) const final;
  void Write(const PciReg32 addr, uint32_t val) const final;
  const char* type(void) const final;

 private:
  ProxyConfig(pci_bdf_t bdf, ddk::PcirootProtocolClient* proto) : Config(bdf), pciroot_(proto) {}
  // The bus driver outlives config objects.
  ddk::PcirootProtocolClient* const pciroot_;
};

}  // namespace pci

#endif  // SRC_DEVICES_BUS_DRIVERS_PCI_CONFIG_H_
