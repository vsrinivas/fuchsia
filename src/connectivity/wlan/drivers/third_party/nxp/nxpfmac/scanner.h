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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_SCANNER_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_SCANNER_H_

#include <fuchsia/hardware/wlan/fullmac/cpp/banjo.h>
#include <lib/fit/function.h>
#include <zircon/compiler.h>

#include <mutex>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/event_handler.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/ioctl_request.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/waitable_state.h"

namespace wlan::nxpfmac {

struct DeviceContext;

class Scanner {
 public:
  using OnScanResult = fit::function<void(const wlan_fullmac_scan_result_t& /*result*/)>;
  using OnScanEnd = fit::callback<void(uint64_t /*txn_id*/, wlan_scan_result_t /*result*/)>;
  Scanner(DeviceContext* context, uint32_t bss_index);
  // Destroying the scanner will stop any ongoing scans and wait for all all calls to the fullmac
  // ifc client to complete.
  ~Scanner();

  // Start a scan. Returns ZX_ERR_ALREADY_EXISTS if a scan is already in progress. The scan will
  // time out after the given timeout has elapsed. Scan results are reported one at a time through
  // the `on_scan_result` callback and `on_scan_end` will be called when the scan ends.
  zx_status_t Scan(const wlan_fullmac_scan_req_t* req, zx_duration_t timeout,
                   OnScanResult&& on_scan_result, OnScanEnd&& on_scan_end) __TA_EXCLUDES(mutex_);

  // Perform a connect scan. This is a quick scan that only scans for the given SSID and only on the
  // given channel. Note that this is a synchronous scan and it's only intended to populate the
  // internal scan result list to allow for a connection to the given BSS. Once this call returns
  // successfully the scan table is populated, other scans might clear it again. Otherwise connect
  // scans behave the same way as a regular scan. They will be blocked if another scan is in
  // progress and they will prevent other scans from starting.
  zx_status_t ConnectScan(const uint8_t* ssid, size_t ssid_len, uint8_t channel,
                          zx_duration_t timeout) __TA_EXCLUDES(mutex_);

  // Stop an ongoing scan. Returns ZX_ERR_NOT_FOUND if no scan is in progress. Stopping an ongoing
  // scan will asynchronously call the on_scan_end callback provided in the Scan call. This is a
  // synchronous call. When the call returns on_scan_end will have already been called and the
  // Scanner is immediately ready to perform a new scan.
  zx_status_t StopScan() __TA_EXCLUDES(mutex_);

 private:
  zx_status_t PrepareScanRequest(const wlan_fullmac_scan_req_t* req) __TA_REQUIRES(mutex_);
  void PopulateScanChannel(wlan_user_scan_chan& user_scan_chan, uint8_t channel, uint8_t scan_type,
                           uint32_t channel_time);
  void OnScanReport(pmlan_event event) __TA_EXCLUDES(mutex_);
  void FetchAndProcessScanResults(wlan_scan_result_t result) __TA_REQUIRES(mutex_);
  void ProcessScanResults(wlan_scan_result_t result) __TA_REQUIRES(mutex_);
  zx_status_t CancelScanIoctl() __TA_REQUIRES(mutex_);
  void EndScan(uint64_t txn_id, wlan_scan_result_t result) __TA_REQUIRES(mutex_);

  DeviceContext* context_ = nullptr;
  using ScanRequestType = IoctlRequest<mlan_ds_scan, sizeof(wlan_user_scan_cfg)>;
  ScanRequestType scan_request_ __TA_GUARDED(mutex_);
  IoctlRequest<mlan_ds_scan> scan_results_ __TA_GUARDED(mutex_);
  WaitableState<bool> scan_in_progress_{false};
  WaitableState<bool> ioctl_in_progress_{false};
  uint64_t txn_id_ __TA_GUARDED(mutex_) = 0;
  const uint32_t bss_index_;
  std::mutex mutex_;

  OnScanResult on_scan_result_;
  OnScanEnd on_scan_end_;
  EventRegistration on_scan_report_event_;
};

}  // namespace wlan::nxpfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_SCANNER_H_
