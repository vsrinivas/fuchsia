// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_interrupt_handlers.h"

#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/regs.h"

namespace wlan {
namespace brcmfmac {

PcieSleepInterruptHandler::PcieSleepInterruptHandler(PcieInterruptProvider* interrupt_provider,
                                                     PcieBuscore* buscore, PcieFirmware* firmware)
    : interrupt_provider_(interrupt_provider),
      buscore_(buscore),
      d2h_mb_data_address_(firmware->GetDeviceToHostMailboxDataAddress()) {
  interrupt_provider->AddInterruptHandler(this);
}

PcieSleepInterruptHandler::~PcieSleepInterruptHandler() {
  interrupt_provider_->RemoveInterruptHandler(this);
}

uint32_t PcieSleepInterruptHandler::HandleInterrupt(uint32_t mailboxint) {
  zx_status_t status = ZX_OK;

  constexpr uint32_t kInterruptMask = (BRCMF_PCIE_MB_INT_FN0_0 | BRCMF_PCIE_MB_INT_FN0_1);
  if ((mailboxint & kInterruptMask) == 0) {
    return 0;
  }

  uint32_t d2h_mb_data = 0;
  if ((status = buscore_->TcmRead<uint32_t>(d2h_mb_data_address_, &d2h_mb_data)) != ZX_OK) {
    BRCMF_ERR("Failed to read device to host mailbox: %s", zx_status_get_string(status));
  }
  if (d2h_mb_data != 0) {
    if ((status = buscore_->TcmWrite<uint32_t>(d2h_mb_data_address_, 0)) != ZX_OK) {
      BRCMF_ERR("Failed to write device to host mailbox: %s", zx_status_get_string(status));
    }
    BRCMF_ERR("PCIE sleep states not supported, d2h_mb_data=0x%08x", d2h_mb_data);
  }

  return kInterruptMask;
}

PcieConsoleInterruptHandler::PcieConsoleInterruptHandler(PcieInterruptProvider* interrupt_provider,
                                                         PcieFirmware* firmware)
    : interrupt_provider_(interrupt_provider), firmware_(firmware) {
  interrupt_provider_->AddInterruptHandler(this);
}

PcieConsoleInterruptHandler::~PcieConsoleInterruptHandler() {
  interrupt_provider_->RemoveInterruptHandler(this);
}

uint32_t PcieConsoleInterruptHandler::HandleInterrupt(uint32_t mailboxint) {
  // Delegate to a function named "Log", so it will show up in the console print as "Log".
  Log();
  return 0;
}

void PcieConsoleInterruptHandler::Log() {
  std::string console;
  while (!(console = firmware_->ReadConsole()).empty()) {
    BRCMF_INFO("%s", console.c_str());
  }
}

}  // namespace brcmfmac
}  // namespace wlan
