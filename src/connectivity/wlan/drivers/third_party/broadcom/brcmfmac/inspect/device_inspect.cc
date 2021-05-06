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

  inspect->rx_freeze_ = inspect->root_.CreateUint("rx_freeze", 0);
  if ((status = inspect->rx_freeze_24hrs_.Init(&inspect->root_, 24, "rx_freeze_24hrs", 0)) !=
      ZX_OK) {
    return status;
  }

  inspect->sdio_max_tx_seq_err_ = inspect->root_.CreateUint("sdio_max_tx_seq_err", 0);
  if ((status = inspect->sdio_max_tx_seq_err_24hrs_.Init(
           &inspect->root_, 24, "sdio_max_tx_seq_err_24hrs", 0)) != ZX_OK) {
    return status;
  }

  inspect->ap_set_ssid_err_ = inspect->root_.CreateUint("ap_set_ssid_err", 0);
  if ((status = inspect->ap_set_ssid_err_24hrs_.Init(&inspect->root_, 24, "ap_set_ssid_err_24hrs",
                                                     0)) != ZX_OK) {
    return status;
  }

  DeviceConnMetrics& conn_metrics_ = inspect->conn_metrics_;
  conn_metrics_.root = inspect->root_.CreateChild("connection-metrics");
  conn_metrics_.success = conn_metrics_.root.CreateUint("success", 0);
  if ((status = conn_metrics_.success_24hrs.Init(&conn_metrics_.root, 24, "success_24hrs", 0)) !=
      ZX_OK) {
    return status;
  }
  conn_metrics_.no_network_fail = conn_metrics_.root.CreateUint("no_network_fail", 0);
  if ((status = conn_metrics_.no_network_fail_24hrs.Init(&conn_metrics_.root, 24,
                                                         "no_network_fail_24hrs", 0)) != ZX_OK) {
    return status;
  }
  conn_metrics_.auth_fail = conn_metrics_.root.CreateUint("auth_fail", 0);
  if ((status = conn_metrics_.auth_fail_24hrs.Init(&conn_metrics_.root, 24, "auth_fail_24hrs",
                                                   0)) != ZX_OK) {
    return status;
  }
  conn_metrics_.other_fail = conn_metrics_.root.CreateUint("other_fail", 0);
  if ((status = conn_metrics_.other_fail_24hrs.Init(&conn_metrics_.root, 24, "other_fail_24hrs",
                                                    0)) != ZX_OK) {
    return status;
  }

  // Start timers.
  constexpr bool kPeriodic = true;
  inspect->timer_hr_ = std::make_unique<Timer>(
      dispatcher,
      [inspect = inspect.get()]() {
        inspect->tx_qfull_24hrs_.SlideWindow();
        inspect->fw_recovered_24hrs_.SlideWindow();
        inspect->rx_freeze_24hrs_.SlideWindow();
        inspect->sdio_max_tx_seq_err_24hrs_.SlideWindow();
        inspect->ap_set_ssid_err_24hrs_.SlideWindow();
        inspect->conn_metrics_.success_24hrs.SlideWindow();
        inspect->conn_metrics_.no_network_fail_24hrs.SlideWindow();
        inspect->conn_metrics_.auth_fail_24hrs.SlideWindow();
        inspect->conn_metrics_.other_fail_24hrs.SlideWindow();
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

void DeviceInspect::LogConnSuccess() {
  conn_metrics_.success.Add(1);
  conn_metrics_.success_24hrs.Add(1);
}
void DeviceInspect::LogConnAuthFail() {
  conn_metrics_.auth_fail.Add(1);
  conn_metrics_.auth_fail_24hrs.Add(1);
}
void DeviceInspect::LogConnNoNetworkFail() {
  conn_metrics_.no_network_fail.Add(1);
  conn_metrics_.no_network_fail_24hrs.Add(1);
}
void DeviceInspect::LogConnOtherFail() {
  conn_metrics_.other_fail.Add(1);
  conn_metrics_.other_fail_24hrs.Add(1);
}

void DeviceInspect::LogRxFreeze() {
  rx_freeze_.Add(1);
  rx_freeze_24hrs_.Add(1);
}

void DeviceInspect::LogSdioMaxTxSeqErr() {
  sdio_max_tx_seq_err_.Add(1);
  sdio_max_tx_seq_err_24hrs_.Add(1);
}

void DeviceInspect::LogApSetSsidErr() {
  ap_set_ssid_err_.Add(1);
  ap_set_ssid_err_24hrs_.Add(1);
}

}  // namespace wlan::brcmfmac
