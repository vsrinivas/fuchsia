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

SoftAp::SoftAp(SoftApIfc* ifc, DeviceContext* context, uint32_t bss_index)
    : ifc_(ifc), context_(context), bss_index_(bss_index) {
  client_connect_event_ = context_->event_handler_->RegisterForInterfaceEvent(
      MLAN_EVENT_ID_UAP_FW_STA_CONNECT, bss_index,
      [this](pmlan_event event) { OnStaConnect(event); });
  client_disconnect_event_ = context_->event_handler_->RegisterForInterfaceEvent(
      MLAN_EVENT_ID_UAP_FW_STA_DISCONNECT, bss_index,
      [this](pmlan_event event) { OnStaDisconnect(event); });
}

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

  // Get the supported data rates for the specified band.
  auto rate_req = IoctlRequest<mlan_ds_rate>(MLAN_IOCTL_RATE, MLAN_ACT_GET, 0,
                                             {.sub_command = MLAN_OID_SUPPORTED_RATES});
  auto& data_rates = rate_req.UserReq().param.rates;
  auto& rate_band_cfg = rate_req.UserReq().param.rate_band_cfg;
  rate_band_cfg.bss_mode = MLAN_BSS_MODE_INFRA;
  rate_band_cfg.config_bands =
      (band_from_channel(req->channel) == BAND_5GHZ) ? BAND_A : (BAND_B | BAND_G);
  io_status = context_->ioctl_adapter_->IssueIoctlSync(&rate_req);
  if (io_status != IoctlStatus::Success) {
    NXPF_ERR("Rate req get failed: %d", io_status);
  } else {
    // Copy the data rates returned by the last ioctl request.
    memcpy(bss_cfg.rates, data_rates, sizeof(bss_cfg.rates));
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

void SoftAp::OnStaConnect(pmlan_event event) {
  // Handle the STA connect event
  if (event->event_len < ETH_ALEN) {
    NXPF_ERR("Invalid STA connect event len: %d", event->event_len);
    return;
  }
  uint8_t* sta_mac = event->event_buf;
  uint8_t* ies = event->event_buf + ETH_ALEN;
  uint32_t ie_len = event->event_len - ETH_ALEN;
  if (!ie_len)
    ies = nullptr;
  ifc_->OnStaConnectEvent(sta_mac, ies, ie_len);
}

void SoftAp::OnStaDisconnect(pmlan_event event) {
  // Handle the STA connect event
  if (event->event_len < ETH_ALEN + sizeof(uint16_t)) {
    NXPF_ERR("STA Disconnect invalid event len: %d", event->event_len);
    return;
  }
  auto reason_code = reinterpret_cast<uint16_t*>(event->event_buf);
  uint8_t* sta_mac = event->event_buf + sizeof(uint16_t);
  ifc_->OnStaDisconnectEvent(sta_mac, *reason_code);
}

}  // namespace wlan::nxpfmac
