// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_interrupt_handlers.h"

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/regs.h"

namespace wlan {
namespace brcmfmac {

PcieSleepInterruptHandler::PcieSleepInterruptHandler(PcieInterruptMaster* interrupt_master,
                                                     PcieBuscore* buscore, PcieFirmware* firmware)
    : interrupt_master_(interrupt_master),
      buscore_(buscore),
      d2h_mb_data_address_(firmware->GetDeviceToHostMailboxDataAddress()) {
  interrupt_master->AddInterruptHandler(this);
}

PcieSleepInterruptHandler::~PcieSleepInterruptHandler() {
  interrupt_master_->RemoveInterruptHandler(this);
}

uint32_t PcieSleepInterruptHandler::HandleInterrupt(uint32_t mailboxint) {
  constexpr uint32_t kInterruptMask = (BRCMF_PCIE_MB_INT_FN0_0 | BRCMF_PCIE_MB_INT_FN0_1);
  if ((mailboxint & kInterruptMask) == 0) {
    return 0;
  }

  const uint32_t d2h_mb_data = buscore_->TcmRead<uint32_t>(d2h_mb_data_address_);
  if (d2h_mb_data != 0) {
    buscore_->TcmWrite<uint32_t>(d2h_mb_data_address_, 0);
    BRCMF_ERR("PCIE sleep states not supported, d2h_mb_data=0x%08x\n", d2h_mb_data);
  }

  return kInterruptMask;
}

PcieConsoleInterruptHandler::PcieConsoleInterruptHandler(PcieInterruptMaster* interrupt_master,
                                                         PcieFirmware* firmware)
    : interrupt_master_(interrupt_master), firmware_(firmware) {
  interrupt_master_->AddInterruptHandler(this);
}

PcieConsoleInterruptHandler::~PcieConsoleInterruptHandler() {
  interrupt_master_->RemoveInterruptHandler(this);
}

uint32_t PcieConsoleInterruptHandler::HandleInterrupt(uint32_t mailboxint) {
  // Delegate to a function named "Log", so it will show up in the console print as "Log".
  Log();
  return 0;
}

void PcieConsoleInterruptHandler::Log() {
  std::string console;
  while (!(console = firmware_->ReadConsole()).empty()) {
    BRCMF_INFO("%s\n", console.c_str());
  }
}

}  // namespace brcmfmac
}  // namespace wlan
