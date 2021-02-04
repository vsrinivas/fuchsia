// Copyright (c) 2021 The Fuchsia Authors
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

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/inspect/device_inspect.h"

#include <lib/zx/time.h>
#include <zircon/status.h>

namespace wlan::brcmfmac {

// static
zx_status_t DeviceInspect::Create(async_dispatcher_t* dispatcher,
                                  std::unique_ptr<DeviceInspect>* inspect_out) {
  zx_status_t status = ZX_OK;

  // Initialize the inspector.
  std::unique_ptr<DeviceInspect> inspect(new DeviceInspect());
  inspect->root_ = inspect->inspector_.GetRoot().CreateChild("brcmfmac-phy");

  // Initialize metrics.
  inspect->tx_qfull_ = inspect->root_.CreateUint("tx_qfull", 0);
  if ((status = inspect->tx_qfull_24hrs_.Init(&inspect->root_, 24, "tx_qfull_24hrs", 0)) != ZX_OK) {
    return status;
  }

  inspect->fw_recovered_ = inspect->root_.CreateUint("fw_recovered", 0);
  if ((status = inspect->fw_recovered_24hrs_.Init(&inspect->root_, 24, "fw_recovered_24hrs", 0)) !=
      ZX_OK) {
    return status;
  }

  // Start timers.
  constexpr bool kPeriodic = true;
  inspect->timer_hr_ = std::make_unique<Timer>(
      dispatcher,
      [inspect = inspect.get()]() {
        inspect->tx_qfull_24hrs_.SlideWindow();
        inspect->fw_recovered_24hrs_.SlideWindow();
      },
      kPeriodic);
  inspect->timer_hr_->Start(zx::hour(1).get());

  *inspect_out = std::move(inspect);
  return ZX_OK;
}

void DeviceInspect::LogTxQueueFull() {
  tx_qfull_.Add(1);
  tx_qfull_24hrs_.Add(1);
}

void DeviceInspect::LogFwRecovered() {
  fw_recovered_.Add(1);
  fw_recovered_24hrs_.Add(1);
}

}  // namespace wlan::brcmfmac
