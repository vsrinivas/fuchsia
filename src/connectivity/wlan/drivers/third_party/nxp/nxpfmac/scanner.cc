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
#include <lib/sync/completion.h>
#include <lib/zx/clock.h>
#include <netinet/if_ether.h>

#include <unordered_set>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/device_context.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/event_handler.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/ioctl_adapter.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/utils.h"

namespace {

constexpr uint32_t kBeaconFixedSize = 12u;

}  // namespace

namespace wlan::nxpfmac {

Scanner::Scanner(DeviceContext* context, uint32_t bss_index)
    : context_(context), bss_index_(bss_index) {
  on_scan_report_event_ = context_->event_handler_->RegisterForInterfaceEvent(
      MLAN_EVENT_ID_DRV_SCAN_REPORT, bss_index, [this](pmlan_event event) { OnScanReport(event); });
}

Scanner::~Scanner() { StopScan(); }

zx_status_t Scanner::Scan(const wlan_fullmac_scan_req_t* req, zx_duration_t timeout,
                          OnScanResult&& on_scan_result, OnScanEnd&& on_scan_end) {
  const std::lock_guard lock(mutex_);
  if (scan_in_progress_) {
    return ZX_ERR_ALREADY_EXISTS;
  }

  const zx_status_t status = PrepareScanRequest(req);
  if (status != ZX_OK) {
    return status;
  }

  on_scan_end_ = std::move(on_scan_end);
  on_scan_result_ = std::move(on_scan_result);

  // Callback for when the scan completes.
  auto on_ioctl_complete = [this](mlan_ioctl_req* req, IoctlStatus io_status) {
    const std::lock_guard lock(mutex_);

    // The ioctl_in_progress_ flag must be cleared whenever this callback exits.
    fit::deferred_callback clear_awaiting_scan_ioctl([this]() { ioctl_in_progress_ = false; });

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
    ioctl_in_progress_ = false;
    on_scan_end_ = nullptr;
    on_scan_result_ = nullptr;
    return ZX_ERR_INTERNAL;
  }

  txn_id_ = req->txn_id;
  scan_in_progress_ = true;

  return ZX_OK;
}

zx_status_t Scanner::ConnectScan(const uint8_t* ssid, size_t ssid_len, uint8_t channel,
                                 zx_duration_t timeout) {
  cssid ssid_data = {.len = static_cast<uint8_t>(ssid_len)};
  if (ssid_len > sizeof(ssid_data.data)) {
    NXPF_ERR("SSID length %zu exceeds maximum length %zu", ssid_len, sizeof(ssid_data.data));
    return ZX_ERR_INVALID_ARGS;
  }
  memcpy(ssid_data.data, ssid, ssid_len);

  wlan_fullmac_scan_req request{.scan_type = WLAN_SCAN_TYPE_ACTIVE,
                                .channels_list = &channel,
                                .channels_count = 1,
                                .ssids_list = &ssid_data,
                                .ssids_count = 1};

  sync_completion_t scan_complete;
  wlan_scan_result_t scan_result = WLAN_SCAN_RESULT_INTERNAL_ERROR;
  auto on_scan_end = [&](uint64_t, wlan_scan_result_t result) {
    scan_result = result;
    sync_completion_signal(&scan_complete);
  };

  zx_status_t status = Scan(&request, timeout, nullptr, std::move(on_scan_end));
  if (status != ZX_OK) {
    NXPF_ERR("Connect scan failed: %s", zx_status_get_string(status));
    return status;
  }

  status = sync_completion_wait(&scan_complete, ZX_TIME_INFINITE);
  if (status != ZX_OK) {
    NXPF_ERR("Failed to wait for connect scan to complete: %s", zx_status_get_string(status));
    return status;
  }

  if (scan_result != WLAN_SCAN_RESULT_SUCCESS) {
    NXPF_ERR("Connect scan failed: %u", scan_result);
    if (scan_result == WLAN_SCAN_RESULT_CANCELED_BY_DRIVER_OR_FIRMWARE) {
      return ZX_ERR_CANCELED;
    }
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

// Unfortunately std::unique_lock is not annotated for thread analysis but it's needed for the
// waitable state. Disable thread safety analysis to work around this.
zx_status_t Scanner::StopScan() __TA_NO_THREAD_SAFETY_ANALYSIS {
  std::unique_lock lock(mutex_);

  // These two checks ensure that both scan_in_progress_ and ioctl_in_progress_ are false by the
  // time this method returns and that we don't attempt to stop anything if either scanning or the
  // ioctl is complete. If one is done then the other is also done or will be done shortly. This
  // ensures that when this method returns the Scanner is safe to shut down or reuse without any
  // unexpected callbacks happening.
  if (!scan_in_progress_) {
    ioctl_in_progress_.Wait(lock, false);
    return ZX_ERR_NOT_FOUND;
  }
  if (!ioctl_in_progress_) {
    scan_in_progress_.Wait(lock, false);
    return ZX_ERR_NOT_FOUND;
  }

  IoctlRequest<mlan_ds_scan> cancel_request(MLAN_IOCTL_SCAN, MLAN_ACT_SET, bss_index_,
                                            mlan_ds_scan{.sub_command = MLAN_OID_SCAN_CANCEL});
  IoctlStatus io_status = context_->ioctl_adapter_->IssueIoctlSync(&cancel_request);
  if (io_status != IoctlStatus::Success) {
    NXPF_ERR("Failed to cancel scan: %u", io_status);
    return ZX_ERR_IO;
  }

  scan_in_progress_.Wait(lock, false);

  return ZX_OK;
}

zx_status_t Scanner::PrepareScanRequest(const wlan_fullmac_scan_req_t* req) {
  if (req->ssids_count > MRVDRV_MAX_SSID_LIST_LENGTH) {
    NXPF_ERR("Requested %zu SSIDs in scan but only %d supported", req->ssids_count,
             MRVDRV_MAX_SSID_LIST_LENGTH);
    return ZX_ERR_INVALID_ARGS;
  }

  if (req->channels_count > WLAN_USER_SCAN_CHAN_MAX) {
    NXPF_ERR("Requested %zu channels in scan but only %d supported", req->channels_count,
             WLAN_USER_SCAN_CHAN_MAX);
    return ZX_ERR_INVALID_ARGS;
  }

  uint8_t scan_type = 0;
  switch (req->scan_type) {
    case WLAN_SCAN_TYPE_ACTIVE:
      scan_type = MLAN_SCAN_TYPE_ACTIVE;
      break;
    case WLAN_SCAN_TYPE_PASSIVE:
      scan_type = MLAN_SCAN_TYPE_PASSIVE;
      break;
    default:
      NXPF_ERR("Invalid scan type %u requested", req->scan_type);
      return ZX_ERR_INVALID_ARGS;
  }

  scan_request_ = ScanRequestType(MLAN_IOCTL_SCAN, MLAN_ACT_SET, bss_index_,
                                  {.sub_command = MLAN_OID_SCAN_USER_CONFIG});
  auto scan_cfg =
      reinterpret_cast<wlan_user_scan_cfg*>(scan_request_.UserReq().param.user_scan.scan_cfg_buf);
  scan_cfg->ext_scan_type = EXT_SCAN_ENHANCE;

  // Copy SSIDs if this is a targeted scan.
  for (size_t i = 0; i < req->ssids_count; ++i) {
    const uint8_t len =
        std::min<uint8_t>(req->ssids_list[i].len, sizeof(scan_cfg->ssid_list[i].ssid));
    memcpy(scan_cfg->ssid_list[i].ssid, req->ssids_list[i].data, len);
    // Leave ssid_list[i].max_len set to zero here, based on other drivers it doesn't seem necessary
    // to set it.
  }

  // Retrieve a list of supported channels. This just calls into mlan and doesn't reach firmware so
  // it's a quick, synchronous call.
  IoctlRequest<mlan_ds_bss> get_channels(MLAN_IOCTL_BSS, MLAN_ACT_GET, bss_index_,
                                         {.sub_command = MLAN_OID_BSS_CHANNEL_LIST});
  const IoctlStatus io_status = context_->ioctl_adapter_->IssueIoctlSync(&get_channels);
  if (io_status != IoctlStatus::Success) {
    // This should only ever fail if the request is malformed.
    NXPF_ERR("Couldn't get channels: %d", io_status);
    return ZX_ERR_INTERNAL;
  }
  auto& chanlist = get_channels.UserReq().param.chanlist;

  if (req->channels_count > 0) {
    // Create a set of all supported channels so we can filter out any requested channels that are
    // not supported.
    std::unordered_set<uint32_t> supported_channels;
    for (size_t i = 0; i < chanlist.num_of_chan; ++i) {
      supported_channels.insert(chanlist.cf[i].channel);
    }

    // Channels to scan provided in request, copy them.
    for (size_t i = 0; i < req->channels_count; ++i) {
      if (supported_channels.find(req->channels_list[i]) != supported_channels.end()) {
        PopulateScanChannel(scan_cfg->chan_list[i], req->channels_list[i], scan_type,
                            req->min_channel_time);
      }
    }
  } else {
    for (size_t i = 0; i < chanlist.num_of_chan && i < WLAN_USER_SCAN_CHAN_MAX; ++i) {
      PopulateScanChannel(scan_cfg->chan_list[i], static_cast<uint8_t>(chanlist.cf[i].channel),
                          scan_type, req->min_channel_time);
    }
  }
  return ZX_OK;
}

void Scanner::PopulateScanChannel(wlan_user_scan_chan& user_scan_chan, uint8_t channel,
                                  uint8_t scan_type, uint32_t channel_time) {
  user_scan_chan.chan_number = channel;
  if (is_dfs_channel(channel) && scan_type == MLAN_SCAN_TYPE_ACTIVE) {
    user_scan_chan.scan_type = MLAN_SCAN_TYPE_PASSIVE_TO_ACTIVE;
  } else {
    user_scan_chan.scan_type = scan_type;
  }
  user_scan_chan.radio_type = band_from_channel(channel);
  user_scan_chan.scan_time = channel_time;
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

    if (on_scan_result_) {
      on_scan_result_(scan_result);
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
  if (on_scan_end_) {
    on_scan_end_(txn_id, result);
    // Reset these callbacks here, they will not be used anymore for the scan that's ending. This
    // ensures that any resources held on to by these callbacks are released.
    on_scan_end_ = nullptr;
    on_scan_result_ = nullptr;
  }
  scan_in_progress_ = false;
}

}  // namespace wlan::nxpfmac
