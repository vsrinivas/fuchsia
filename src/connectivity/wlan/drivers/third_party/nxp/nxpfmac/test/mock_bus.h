// Copyright (c) 2022 The Fuchsia Authors
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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_TEST_MOCK_BUS_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_TEST_MOCK_BUS_H_

#include <functional>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/bus_interface.h"

namespace wlan::nxpfmac {

// A mock Device implementation for unit test purposes. Very little functionality is currently
// supported. Add more as needed.
class MockBus : public BusInterface {
 public:
  // Device overrides
  zx_status_t TriggerMainProcess() override {
    if (trigger_main_process_) {
      return trigger_main_process_();
    }
    return ZX_OK;
  }

  zx_status_t OnMlanRegistered(void *mlan_adapter) override { return ZX_OK; }
  zx_status_t OnFirmwareInitialized() override { return ZX_OK; }

  // Implementation mocks
  void SetTriggerMainProcess(std::function<zx_status_t(void)> &&trigger_main_process) {
    trigger_main_process_ = std::move(trigger_main_process);
  }

 private:
  std::function<zx_status_t(void)> trigger_main_process_;
};

}  // namespace wlan::nxpfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_TEST_MOCK_BUS_H_
