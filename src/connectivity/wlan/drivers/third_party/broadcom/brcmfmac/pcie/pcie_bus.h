// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_BUS_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_BUS_H_

#include <zircon/types.h>

#include <list>
#include <memory>

#include <ddk/device.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcmu_utils.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bus.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/common.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/netbuf.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_interrupt_master.h"

namespace wlan {
namespace brcmfmac {

class Device;
class PcieBuscore;
class PcieFirmware;
class PcieRingMaster;

// This class implements the brcmfmc bus functionality (see: bus.h) for the PCIE bus.  It implements
// the C-style HAL as defined by brcmf_bus_ops and used by the higher-level cfg80211 logic.
class PcieBus {
 public:
  PcieBus();
  ~PcieBus();

  // Static factory function for PcieBus instances.
  static zx_status_t Create(Device* device, std::unique_ptr<PcieBus>* bus_out);

  // Get the brcmf_bus_ops struct that forwards brcmf driver bus calls to a PcieBus instance.
  static const brcmf_bus_ops* GetBusOps();

 private:
  // Bus functionality implemnentation.
  static brcmf_bus_type GetBusType();
  void Stop();
  zx_status_t TxData(brcmf_netbuf* netbuf);
  zx_status_t TxCtl(unsigned char* msg, uint len);
  zx_status_t RxCtl(unsigned char* msg, uint len, int* rxlen_out);
  void WowlConfig(bool enabled);
  size_t GetRamsize();
  zx_status_t GetMemdump(void* data, size_t len);
  zx_status_t GetFwname(uint chip, uint chiprev, unsigned char* fw_name, size_t* fw_name_size);
  zx_status_t GetBootloaderMacaddr(uint8_t* mac_addr);
  zx_status_t GetWifiMetadata(void* config, size_t exp_size, size_t* actual);

 private:
  Device* device_ = nullptr;
  std::unique_ptr<PcieBuscore> pcie_buscore_;
  std::unique_ptr<PcieFirmware> pcie_firmware_;
  std::unique_ptr<PcieRingMaster> pcie_ring_master_;
  std::unique_ptr<PcieInterruptMaster> pcie_interrupt_master_;
  std::list<std::unique_ptr<PcieInterruptMaster::InterruptHandler>> pcie_interrupt_handlers_;
  std::unique_ptr<brcmf_bus> brcmf_bus_;
  std::unique_ptr<brcmf_mp_device> brcmf_mp_device_;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_BUS_H_
