// Copyright (c) 2019 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_BUS_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_BUS_H_

#include <zircon/types.h>

#include <memory>

#include <ddk/device.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/brcmu_utils.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/bus.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/common.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/netbuf.h"

namespace wlan {
namespace brcmfmac {

class Device;
class PcieBuscore;

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

 private:
  Device* device_ = nullptr;
  std::unique_ptr<PcieBuscore> pcie_buscore_;
  std::unique_ptr<brcmf_bus> brcmf_bus_;
  std::unique_ptr<brcmf_mp_device> brcmf_mp_device_;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_BUS_H_
