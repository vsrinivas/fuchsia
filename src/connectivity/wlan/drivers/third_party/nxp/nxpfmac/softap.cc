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

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/softap.h"

#include <fuchsia/wlan/ieee80211/c/banjo.h>
#include <lib/ddk/debug.h>
#include <netinet/if_ether.h>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/device_context.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/event_handler.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/ioctl_adapter.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/utils.h"

namespace wlan::nxpfmac {

SoftAp::SoftAp(DeviceContext* context, uint32_t bss_index)
    : context_(context), bss_index_(bss_index) {}

SoftAp::~SoftAp() {
  // Attempt to stop the Soft AP and ignore the error.
  wlan_fullmac_stop_req req = {.ssid = ssid_};
  Stop(&req);
}

wlan_start_result_t SoftAp::Start(const wlan_fullmac_start_req* req) {
  std::lock_guard lock(mutex_);
  IoctlRequest<mlan_ds_bss> start_req(MLAN_IOCTL_BSS, MLAN_ACT_GET, bss_index_,
                                      {.sub_command = MLAN_OID_UAP_BSS_CONFIG});
  auto& bss_cfg = start_req.UserReq().param.bss_config;
  IoctlStatus io_status;

  if (started_) {
    NXPF_ERR("SoftAP has already been started");
    return WLAN_START_RESULT_BSS_ALREADY_STARTED_OR_JOINED;
  }
  // Get the current BSS configuration
  io_status = context_->ioctl_adapter_->IssueIoctlSync(&start_req);
  if (io_status != IoctlStatus::Success) {
    NXPF_ERR("BSS get req failed: %d", io_status);
    return WLAN_START_RESULT_NOT_SUPPORTED;
  }

  // BSS get should have copied the default config into the ioctl buffer, just set ssid,
  // channel and band from the request
  start_req.IoctlReq().action = MLAN_ACT_SET;
  memcpy(&bss_cfg.ssid.ssid, req->ssid.data, req->ssid.len);
  bss_cfg.ssid.ssid_len = req->ssid.len;
  bss_cfg.channel = req->channel;
  bss_cfg.bandcfg.chanBand = band_from_channel(req->channel);
  bss_cfg.bandcfg.chanWidth = CHAN_BW_20MHZ;
  io_status = context_->ioctl_adapter_->IssueIoctlSync(&start_req);
  if (io_status != IoctlStatus::Success) {
    NXPF_ERR("BSS set req failed: %d", io_status);
    return WLAN_START_RESULT_NOT_SUPPORTED;
  }

  // Now start the BSS
  start_req.IoctlReq().req_id = MLAN_IOCTL_BSS;
  start_req.IoctlReq().action = MLAN_ACT_SET;
  start_req.UserReq().sub_command = MLAN_OID_BSS_START;
  start_req.UserReq().param.host_based = 1;
  io_status = context_->ioctl_adapter_->IssueIoctlSync(&start_req);
  if (io_status != IoctlStatus::Success) {
    NXPF_ERR("BSS start req failed: %d", io_status);
    return WLAN_START_RESULT_NOT_SUPPORTED;
  }
  started_ = true;
  ssid_ = req->ssid;
  return WLAN_START_RESULT_SUCCESS;
}

wlan_stop_result_t SoftAp::Stop(const wlan_fullmac_stop_req* req) {
  std::lock_guard lock(mutex_);
  IoctlRequest<mlan_ds_bss> stop_req(MLAN_IOCTL_BSS, MLAN_ACT_SET, bss_index_,
                                     {.sub_command = MLAN_OID_UAP_BSS_RESET});
  IoctlStatus io_status;

  if (!started_) {
    NXPF_ERR("SoftAP has not been started yet");
    return WLAN_STOP_RESULT_BSS_ALREADY_STOPPED;
  }
  // Ensure the requested ssid matches the started ssid.
  if (memcmp(req->ssid.data, ssid_.data, req->ssid.len) != 0) {
    NXPF_ERR("Stop req ssid: %s does not match started ssid: %s", req->ssid.data, ssid_.data);
    return WLAN_STOP_RESULT_INTERNAL_ERROR;
  }

  // Send stop request to FW.
  io_status = context_->ioctl_adapter_->IssueIoctlSync(&stop_req);
  if (io_status != IoctlStatus::Success) {
    NXPF_ERR("BSS stop req failed: %d", io_status);
    return WLAN_STOP_RESULT_INTERNAL_ERROR;
  }

  started_ = false;
  return WLAN_STOP_RESULT_SUCCESS;
}

}  // namespace wlan::nxpfmac
