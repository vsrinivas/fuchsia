// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bus/drivers/pci/config.h"

#include <assert.h>
#include <fuchsia/hardware/pciroot/cpp/banjo.h>
#include <inttypes.h>
#include <lib/ddk/debug.h>

#include <pretty/hexdump.h>

#include "src/devices/bus/drivers/pci/common.h"

namespace pci {

// MMIO Config Implementation
zx_status_t MmioConfig::Create(pci_bdf_t bdf, ddk::MmioBuffer* ecam, uint8_t start_bus,
                               uint8_t end_bus, std::unique_ptr<Config>* config) {
  if (bdf.bus_id < start_bus || bdf.bus_id > end_bus || bdf.device_id >= PCI_MAX_DEVICES_PER_BUS ||
      bdf.function_id >= PCI_MAX_FUNCTIONS_PER_DEVICE) {
    return ZX_ERR_INVALID_ARGS;
  }

  ddk::MmioView view = ecam->View(bdf_to_ecam_offset(bdf, start_bus), PCIE_EXTENDED_CONFIG_SIZE);
  *config = std::unique_ptr<MmioConfig>(new MmioConfig(bdf, std::move(view)));
  return ZX_OK;
}

uint8_t MmioConfig::Read(const PciReg8 addr) const { return view_.Read<uint8_t>(addr.offset()); }

uint16_t MmioConfig::Read(const PciReg16 addr) const { return view_.Read<uint16_t>(addr.offset()); }

uint32_t MmioConfig::Read(const PciReg32 addr) const { return view_.Read<uint32_t>(addr.offset()); }

void MmioConfig::Write(PciReg8 addr, uint8_t val) const { view_.Write(val, addr.offset()); }

void MmioConfig::Write(PciReg16 addr, uint16_t val) const { view_.Write(val, addr.offset()); }

void MmioConfig::Write(PciReg32 addr, uint32_t val) const { view_.Write(val, addr.offset()); }

const char* MmioConfig::type() const { return "mmio"; }

// Proxy Config Implementation
zx_status_t ProxyConfig::Create(pci_bdf_t bdf, ddk::PcirootProtocolClient* proto,
                                std::unique_ptr<Config>* config) {
  // Can't use std::make_unique because the constructor is private.
  *config = std::unique_ptr<ProxyConfig>(new ProxyConfig(bdf, proto));
  return ZX_OK;
}

uint8_t ProxyConfig::Read(const PciReg8 addr) const {
  uint8_t tmp;
  ZX_ASSERT(pciroot_->ConfigRead8(&bdf(), addr.offset(), &tmp) == ZX_OK);
  return tmp;
}

uint16_t ProxyConfig::Read(const PciReg16 addr) const {
  uint16_t tmp;
  ZX_ASSERT(pciroot_->ConfigRead16(&bdf(), addr.offset(), &tmp) == ZX_OK);
  return tmp;
}

uint32_t ProxyConfig::Read(const PciReg32 addr) const {
  uint32_t tmp;
  ZX_ASSERT(pciroot_->ConfigRead32(&bdf(), addr.offset(), &tmp) == ZX_OK);
  return tmp;
}

void ProxyConfig::Write(PciReg8 addr, uint8_t val) const {
  ZX_ASSERT(pciroot_->ConfigWrite8(&bdf(), addr.offset(), val) == ZX_OK);
}

void ProxyConfig::Write(PciReg16 addr, uint16_t val) const {
  ZX_ASSERT(pciroot_->ConfigWrite16(&bdf(), addr.offset(), val) == ZX_OK);
}

void ProxyConfig::Write(PciReg32 addr, uint32_t val) const {
  ZX_ASSERT(pciroot_->ConfigWrite32(&bdf(), addr.offset(), val) == ZX_OK);
}

const char* ProxyConfig::type() const { return "proxy"; }

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

}  // namespace pci
