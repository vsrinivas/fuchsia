// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_INTERRUPT_HANDLERS_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_INTERRUPT_HANDLERS_H_

// This file defines some interrupt handlers used by the PCIE bus.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_buscore.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_firmware.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_interrupt_provider.h"

namespace wlan {
namespace brcmfmac {

// An InterruptHandler implemenation that handles PCIE bus sleep notifications.
class PcieSleepInterruptHandler : public PcieInterruptProvider::InterruptHandler {
 public:
  explicit PcieSleepInterruptHandler(PcieInterruptProvider* interrupt_provider,
                                     PcieBuscore* buscore, PcieFirmware* firmware);
  ~PcieSleepInterruptHandler() override;
  uint32_t HandleInterrupt(uint32_t mailboxint) override;

 private:
  PcieInterruptProvider* const interrupt_provider_ = nullptr;
  PcieBuscore* const buscore_ = nullptr;
  const uint32_t d2h_mb_data_address_ = 0;
};

// An InterruptHandler implementation that logs firmware console output.
class PcieConsoleInterruptHandler : public PcieInterruptProvider::InterruptHandler {
 public:
  explicit PcieConsoleInterruptHandler(PcieInterruptProvider* interrupt_provider,
                                       PcieFirmware* firmware);
  ~PcieConsoleInterruptHandler() override;
  uint32_t HandleInterrupt(uint32_t mailboxint) override;

 private:
  void Log();

  PcieInterruptProvider* const interrupt_provider_;
  PcieFirmware* const firmware_;
};

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_INTERRUPT_HANDLERS_H_
