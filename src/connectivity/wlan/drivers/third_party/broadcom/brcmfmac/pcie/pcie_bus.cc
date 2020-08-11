// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_bus.h"

#include <zircon/errors.h>

#include <ddk/metadata.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/firmware.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/core.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/device.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_buscore.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_firmware.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_interrupt_handlers.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_interrupt_provider.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_ring_provider.h"

namespace wlan {
namespace brcmfmac {

PcieBus::PcieBus() = default;

PcieBus::~PcieBus() {
  while (!pcie_interrupt_handlers_.empty()) {
    // Pop them back to front for a defined order of destruction.
    pcie_interrupt_handlers_.pop_back();
  }
  if (device_ != nullptr) {
    if (device_->drvr()->settings == brcmf_mp_device_.get()) {
      device_->drvr()->settings = nullptr;
    }
    if (device_->drvr()->bus_if == brcmf_bus_.get()) {
      device_->drvr()->bus_if = nullptr;
    }
    device_ = nullptr;
  }
}

// static
zx_status_t PcieBus::Create(Device* device, std::unique_ptr<PcieBus>* bus_out) {
  zx_status_t status = ZX_OK;
  auto pcie_bus = std::make_unique<PcieBus>();

  std::unique_ptr<PcieBuscore> pcie_buscore;
  if ((status = PcieBuscore::Create(device->parent(), &pcie_buscore)) != ZX_OK) {
    return status;
  }

  std::unique_ptr<PcieFirmware> pcie_firmware;
  if ((status = PcieFirmware::Create(device, pcie_buscore.get(), &pcie_firmware)) != ZX_OK) {
    return status;
  }

  std::unique_ptr<PcieRingProvider> pcie_ring_provider;
  if ((status = PcieRingProvider::Create(pcie_buscore.get(), pcie_firmware.get(),
                                         &pcie_ring_provider)) != ZX_OK) {
    return status;
  }

  std::unique_ptr<PcieInterruptProvider> pcie_interrupt_provider;
  if ((status = PcieInterruptProvider::Create(device->parent(), pcie_buscore.get(),
                                              &pcie_interrupt_provider)) != ZX_OK) {
    return status;
  }

  std::list<std::unique_ptr<InterruptProviderInterface::InterruptHandler>> pcie_interrupt_handlers;
  pcie_interrupt_handlers.emplace_back(new PcieSleepInterruptHandler(
      pcie_interrupt_provider.get(), pcie_buscore.get(), pcie_firmware.get()));
  pcie_interrupt_handlers.emplace_back(
      new PcieConsoleInterruptHandler(pcie_interrupt_provider.get(), pcie_firmware.get()));

  auto bus = std::make_unique<brcmf_bus>();
  bus->bus_priv.pcie = pcie_bus.get();
  bus->ops = PcieBus::GetBusOps();

  auto mp_device = std::make_unique<brcmf_mp_device>();
  brcmf_get_module_param(brcmf_bus_type::BRCMF_BUS_TYPE_PCIE, pcie_buscore->chip()->chip,
                         pcie_buscore->chip()->chiprev, mp_device.get());

  device->drvr()->bus_if = bus.get();
  device->drvr()->settings = mp_device.get();

  pcie_bus->device_ = device;
  pcie_bus->pcie_buscore_ = std::move(pcie_buscore);
  pcie_bus->pcie_firmware_ = std::move(pcie_firmware);
  pcie_bus->pcie_ring_provider_ = std::move(pcie_ring_provider);
  pcie_bus->pcie_interrupt_provider_ = std::move(pcie_interrupt_provider);
  pcie_bus->pcie_interrupt_handlers_ = std::move(pcie_interrupt_handlers);
  pcie_bus->brcmf_bus_ = std::move(bus);
  pcie_bus->brcmf_mp_device_ = std::move(mp_device);

  *bus_out = std::move(pcie_bus);
  return ZX_OK;
}

// static
const brcmf_bus_ops* PcieBus::GetBusOps() {
  static constexpr brcmf_bus_ops bus_ops = {
      .get_bus_type = []() { return PcieBus::GetBusType(); },
      .get_bootloader_macaddr =
          [](brcmf_bus* bus, uint8_t* mac_addr) {
            return bus->bus_priv.pcie->GetBootloaderMacaddr(mac_addr);
          },
      .get_wifi_metadata =
          [](brcmf_bus* bus, void* config, size_t exp_size, size_t* actual) {
            return bus->bus_priv.pcie->GetWifiMetadata(config, exp_size, actual);
          },
  };
  return &bus_ops;
}

DmaBufferProviderInterface* PcieBus::GetDmaBufferProvider() { return pcie_buscore_.get(); }

DmaRingProviderInterface* PcieBus::GetDmaRingProvider() { return pcie_ring_provider_.get(); }

InterruptProviderInterface* PcieBus::GetInterruptProvider() {
  return pcie_interrupt_provider_.get();
}

// static
brcmf_bus_type PcieBus::GetBusType() { return BRCMF_BUS_TYPE_PCIE; }

zx_status_t PcieBus::GetBootloaderMacaddr(uint8_t* mac_addr) {
  BRCMF_ERR("PcieBus::GetBootloaderMacaddr unimplemented");
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t PcieBus::GetWifiMetadata(void* config, size_t exp_size, size_t* actual) {
  return device_->DeviceGetMetadata(DEVICE_METADATA_WIFI_CONFIG, config, exp_size, actual);
}

}  // namespace brcmfmac
}  // namespace wlan
