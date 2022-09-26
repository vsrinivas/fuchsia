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

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/scanner.h"

#include <lib/fit/defer.h>
#include <lib/zx/clock.h>
#include <netinet/if_ether.h>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/device_context.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/event_handler.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/ioctl_adapter.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/mlan.h"

namespace {

constexpr uint32_t kBeaconFixedSize = 12u;

}  // namespace

namespace wlan::nxpfmac {

Scanner::Scanner(ddk::WlanFullmacImplIfcProtocolClient* fullmac_ifc, DeviceContext* context,
                 uint32_t bss_index)
    : context_(context), bss_index_(bss_index), fullmac_ifc_(fullmac_ifc) {
  on_scan_report_event_ = context_->event_handler_->RegisterForInterfaceEvent(
      MLAN_EVENT_ID_DRV_SCAN_REPORT, bss_index, [this](pmlan_event event) { OnScanReport(event); });
}

Scanner::~Scanner() {
  StopScan();
  // Wait for scan_in_progress_ and ioctl_in_progress_ to become false before completing
  // destruction. This ensures that there are no pending ioctls or scan reports that would call into
  // a destructed object.
  std::unique_lock lock(mutex_);
  scan_in_progress_.Wait(lock, false);
  ioctl_in_progress_.Wait(lock, false);
}

zx_status_t Scanner::Scan(const wlan_fullmac_scan_req_t* req, zx_duration_t timeout) {
  const std::lock_guard lock(mutex_);
  if (scan_in_progress_) {
    return ZX_ERR_ALREADY_EXISTS;
  }

  if (req->ssids_count > 1u) {
    NXPF_ERR("Requested %zu SSIDs in scan but only 1 supported", req->ssids_count);
    return ZX_ERR_INVALID_ARGS;
  }

  // Prepare the request
  scan_request_ = IoctlRequest<mlan_ds_scan>(MLAN_IOCTL_SCAN, MLAN_ACT_SET, bss_index_,
                                             {.sub_command = MLAN_OID_SCAN_NORMAL});
  auto& scan_req = scan_request_.UserReq().param.scan_req;

  switch (req->scan_type) {
    case WLAN_SCAN_TYPE_ACTIVE:
      scan_req.scan_type = MLAN_SCAN_TYPE_ACTIVE;
      scan_req.scan_time.active_scan_time = req->min_channel_time;
      break;
    case WLAN_SCAN_TYPE_PASSIVE:
      scan_req.scan_type = MLAN_SCAN_TYPE_PASSIVE;
      scan_req.scan_time.passive_scan_time = req->min_channel_time;
      break;
    default:
      NXPF_ERR("Invalid scan type %u requested", req->scan_type);
      return ZX_ERR_INVALID_ARGS;
  }

  if (req->ssids_count == 1) {
    // Perform a scan for a specific SSID.
    scan_request_.UserReq().sub_command = MLAN_OID_SCAN_SPECIFIC_SSID;
    const uint8_t len = std::min<uint8_t>(req->ssids_list[0].len, sizeof(scan_req.scan_ssid.ssid));
    memcpy(scan_req.scan_ssid.ssid, req->ssids_list[0].data, len);
    scan_req.scan_ssid.ssid_len = len;
  }

  // Callback for when the scan completes.
  auto on_ioctl_complete = [this](mlan_ioctl_req* req, IoctlStatus io_status) {
    const std::lock_guard lock(mutex_);

    // The ioctl_in_progress_ flag must be cleared whenever this callback exits. The lock on mutex_
    // has to be constructed before this line to ensure that it is held when we set the flag.
    // Unfortunately thread analysis is not clever enough to recognize this so we have to mark the
    // lambda with the no thread analysis annotation.
    fit::deferred_callback clear_awaiting_scan_ioctl(
        [this]() __TA_NO_THREAD_SAFETY_ANALYSIS { ioctl_in_progress_ = false; });

    wlan_scan_result_t result;
    switch (io_status) {
      case IoctlStatus::Success:
        // We don't need to do anything here, we'll get results in the scan report event handler.
        return;
      case IoctlStatus::Timeout:
        NXPF_WARN("Scan timed out");
        // If the scan times out fetch and process whatever partial results we have so far.
        FetchAndProcessScanResults(WLAN_SCAN_RESULT_CANCELED_BY_DRIVER_OR_FIRMWARE);
        return;
      case IoctlStatus::Canceled:
        result = WLAN_SCAN_RESULT_CANCELED_BY_DRIVER_OR_FIRMWARE;
        break;
      default:
        NXPF_ERR("Scan failed: %u", io_status);
        result = WLAN_SCAN_RESULT_INTERNAL_ERROR;
        break;
    }

    if (scan_in_progress_) {
      EndScan(txn_id_, result);
    }
  };

  // This needs to be set before the call to issue the ioctl. It must be set before the ioctl
  // complete callback is called which could potentially happen before IssueIoctl even returns.
  ioctl_in_progress_ = true;

  // Issue request
  const IoctlStatus io_status =
      context_->ioctl_adapter_->IssueIoctl(&scan_request_, std::move(on_ioctl_complete), timeout);
  if (io_status != IoctlStatus::Pending) {
    // We don't expect anything but pending here, the scan cannot complete immediately so even an
    // IoctlStatus::Success is an error.
    NXPF_ERR("Scan ioctl failed: %d", io_status);
    EndScan(req->txn_id, WLAN_SCAN_RESULT_INTERNAL_ERROR);
    ioctl_in_progress_ = false;
    return ZX_ERR_INTERNAL;
  }

  txn_id_ = req->txn_id;
  scan_in_progress_ = true;

  return ZX_OK;
}

zx_status_t Scanner::StopScan() {
  std::lock_guard lock(mutex_);
  if (!scan_in_progress_) {
    return ZX_ERR_NOT_FOUND;
  }

  // Canceling the ioctl will trigger the scan ioctl complete callback with a canceled status,
  // don't end the scan until it completes.
  zx_status_t status = CancelScanIoctl();
  if (status != ZX_OK) {
    NXPF_ERR("Failed to cancel scan ioctl: %s", zx_status_get_string(status));
    return status;
  }
  return ZX_OK;
}

void Scanner::OnScanReport(pmlan_event event) {
  std::lock_guard lock(mutex_);
  if (!scan_in_progress_) {
    NXPF_ERR("Received scan report event but no scan in progress");
    return;
  }
  FetchAndProcessScanResults(WLAN_SCAN_RESULT_SUCCESS);
}

void Scanner::FetchAndProcessScanResults(wlan_scan_result_t result) {
  // Initiate the scan results requests, this will also hold the results when the ioctl completes.
  scan_results_ = IoctlRequest<mlan_ds_scan>(MLAN_IOCTL_SCAN, MLAN_ACT_GET, bss_index_,
                                             mlan_ds_scan{.sub_command = MLAN_OID_SCAN_NORMAL});

  IoctlStatus io_status = context_->ioctl_adapter_->IssueIoctlSync(&scan_results_);
  if (io_status != IoctlStatus::Success) {
    NXPF_ERR("Failed to get scan results: %d", io_status);
    EndScan(txn_id_, WLAN_SCAN_RESULT_INTERNAL_ERROR);
    return;
  }

  ProcessScanResults(result);
}

void Scanner::ProcessScanResults(wlan_scan_result_t result) {
  mlan_scan_resp& response = scan_results_.UserReq().param.scan_resp;

  auto results = reinterpret_cast<pBSSDescriptor_t>(response.pscan_table);

  for (uint32_t i = 0; i < response.num_in_scan_table; i++) {
    int8_t rssi = static_cast<int8_t>(std::clamp(-results[i].rssi, INT8_MIN, 0));
    uint8_t* ies = nullptr;
    size_t ies_count = 0;
    if (results[i].pbeacon_buf && results[i].beacon_buf_size > kBeaconFixedSize) {
      ies = results[i].pbeacon_buf + kBeaconFixedSize;
      ies_count = results[i].beacon_buf_size - kBeaconFixedSize;
    }
    uint16_t cap_info;
    memcpy(&cap_info, &results[i].cap_info, sizeof(cap_info));

    wlan_fullmac_scan_result_t scan_result{
        .txn_id = txn_id_,
        .timestamp_nanos = zx::clock::get_monotonic().get(),
        .bss{
            .bss_type = BSS_TYPE_INFRASTRUCTURE,  // TODO(fxbug.dev/80230): Remove hardcoding?
            .beacon_period = results[i].beacon_period,
            .capability_info = cap_info,
            .ies_list = ies,
            .ies_count = ies_count,
            .channel{.primary = static_cast<uint8_t>(results[i].channel),
                     .cbw = results[i].curr_bandwidth},
            .rssi_dbm = rssi,
        }};
    memcpy(scan_result.bss.bssid, results[i].mac_address, ETH_ALEN);

    if (fullmac_ifc_->is_valid()) {
      fullmac_ifc_->OnScanResult(&scan_result);
    }
  }

  EndScan(txn_id_, result);
}

zx_status_t Scanner::CancelScanIoctl() {
  if (!context_->ioctl_adapter_->CancelIoctl(&scan_request_)) {
    NXPF_ERR("Failed to cancel scan ioctl, no ioctl in progress");
    return ZX_ERR_NOT_FOUND;
  }
  return ZX_OK;
}

void Scanner::EndScan(uint64_t txn_id, wlan_scan_result_t result) {
  if (fullmac_ifc_->is_valid()) {
    const wlan_fullmac_scan_end_t end{.txn_id = txn_id, .code = result};
    fullmac_ifc_->OnScanEnd(&end);
  }
  scan_in_progress_ = false;
}

}  // namespace wlan::nxpfmac
