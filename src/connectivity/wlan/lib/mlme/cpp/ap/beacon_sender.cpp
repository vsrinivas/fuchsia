// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <wlan/common/channel.h>
#include <wlan/common/element_splitter.h>
#include <wlan/common/logging.h>
#include <wlan/common/parse_element.h>
#include <wlan/mlme/ap/beacon_sender.h>
#include <wlan/mlme/ap/bss_interface.h>
#include <wlan/mlme/ap/tim.h>
#include <wlan/mlme/beacon.h>
#include <wlan/mlme/debug.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>
#include <zircon/assert.h>
#include <zircon/status.h>

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

BeaconSender::BeaconSender(DeviceInterface* device) : device_(device) {}

BeaconSender::~BeaconSender() {
  // Ensure Beaconing is stopped when the object is destroyed.
  Stop();
}

void BeaconSender::Start(BssInterface* bss, const PsCfg& ps_cfg,
                         const MlmeMsg<wlan_mlme::StartRequest>& req) {
  ZX_DEBUG_ASSERT(!IsStarted());

  bss_ = bss;
  req.body()->Clone(&req_);

  // Build the template.
  wlan_bcn_config_t bcn_cfg;
  MgmtFrame<Beacon> frame;
  auto status = BuildBeacon(ps_cfg, &frame, &bcn_cfg.tim_ele_offset);
  if (status != ZX_OK) {
    errorf("[bcn-sender] [%s] could not build beacon template: %d\n",
           bss_->bssid().ToString().c_str(), status);
    return;
  }

  // Copy template content.
  auto packet = frame.Take();
  bcn_cfg.tmpl.packet_head.data_size = packet->len();
  bcn_cfg.tmpl.packet_head.data_buffer = packet->data();
  bcn_cfg.beacon_interval = req.body()->beacon_period;
  status = device_->EnableBeaconing(&bcn_cfg);
  if (status != ZX_OK) {
    errorf("[bcn-sender] [%s] could not start beacon sending: %d\n",
           bss_->bssid().ToString().c_str(), status);
    return;
  }

  debugbss("[bcn-sender] [%s] enabled Beacon sending\n",
           bss_->bssid().ToString().c_str());
}

void BeaconSender::Stop() {
  if (!IsStarted()) {
    return;
  }

  auto status = device_->EnableBeaconing(nullptr);
  if (status != ZX_OK) {
    errorf("[bcn-sender] [%s] could not stop beacon sending: %d\n",
           bss_->bssid().ToString().c_str(), status);
    return;
  }

  debugbss("[bcn-sender] [%s] disabled Beacon sending\n",
           bss_->bssid().ToString().c_str());
  bss_ = nullptr;
}

bool BeaconSender::IsStarted() { return bss_ != nullptr; }

static bool SsidMatch(Span<const uint8_t> our_ssid,
                      Span<const uint8_t> req_ssid) {
  return req_ssid.empty()  // wildcard always matches
         || std::equal(our_ssid.begin(), our_ssid.end(), req_ssid.begin(),
                       req_ssid.end());
}

bool ShouldSendProbeResponse(Span<const uint8_t> ie_chain,
                             Span<const uint8_t> our_ssid) {
  auto splitter = common::ElementSplitter(ie_chain);
  auto it = std::find_if(splitter.begin(), splitter.end(), [](auto elem) {
    return std::get<element_id::ElementId>(elem) == element_id::kSsid;
  });

  if (it == splitter.end()) {
    // Request did not contain an SSID IE. Technically this is a malformed probe
    // request since SSID is not optional, but we treat it as a wildcard request
    // anyway. We might want to revisit this idea.
    return true;
  }

  if (auto ssid = common::ParseSsid(std::get<Span<const uint8_t>>(*it))) {
    return SsidMatch(our_ssid, *ssid);
  }
  // Malformed SSID element
  return false;
}

zx_status_t BeaconSender::BuildBeacon(const PsCfg& ps_cfg,
                                      MgmtFrame<Beacon>* frame,
                                      size_t* tim_ele_offset) {
  BeaconConfig c = {
      .bssid = bss_->bssid(),
      .ssid = req_.ssid.data(),
      .ssid_len = req_.ssid.size(),
      .rsne = req_.rsne.is_null() ? nullptr : req_.rsne->data(),
      .rsne_len = req_.rsne->size(),
      .beacon_period = req_.beacon_period,
      .channel = bss_->Chan(),  // looks like we are ignoring 'channel' in
                                // 'req'. Is that correct?
      .ps_cfg = &ps_cfg,
      .ht = bss_->Ht(),
  };
  c.rates = bss_->Rates();
  return ::wlan::BuildBeacon(c, frame, tim_ele_offset);
}

zx_status_t BeaconSender::UpdateBeacon(const PsCfg& ps_cfg) {
  debugfn();
  ZX_DEBUG_ASSERT(IsStarted());
  if (!IsStarted()) {
    return ZX_ERR_BAD_STATE;
  }

  MgmtFrame<Beacon> frame;
  size_t tim_ele_offset;
  BuildBeacon(ps_cfg, &frame, &tim_ele_offset);

  zx_status_t status = device_->ConfigureBeacon(frame.Take());
  if (status != ZX_OK) {
    errorf("[bcn-sender] [%s] could not send beacon packet: %d\n",
           bss_->bssid().ToString().c_str(), status);
    return status;
  }

  return ZX_OK;
}

void BeaconSender::SendProbeResponse(const common::MacAddr& recv_addr,
                                     Span<const uint8_t> ie_chain) {
  if (!IsStarted()) {
    return;
  }
  if (!ShouldSendProbeResponse(ie_chain, req_.ssid)) {
    return;
  }

  BeaconConfig c = {
      .bssid = bss_->bssid(),
      .ssid = req_.ssid.data(),
      .ssid_len = req_.ssid.size(),
      .rsne = req_.rsne.is_null() ? nullptr : req_.rsne->data(),
      .rsne_len = req_.rsne->size(),
      .beacon_period = req_.beacon_period,
      .channel = bss_->Chan(),
      .ps_cfg = nullptr,  // no TIM element in probe response
      .ht = bss_->Ht(),
  };
  c.rates = bss_->Rates();

  MgmtFrame<ProbeResponse> frame;
  zx_status_t status = BuildProbeResponse(c, recv_addr, &frame);
  if (status != ZX_OK) {
    errorf("could not build a probe response frame: %s\n",
           zx_status_get_string(status));
    return;
  }

  auto packet = frame.Take();
  status = device_->SendWlan(std::move(packet));
  if (status != ZX_OK) {
    errorf("[bcn-sender] [%s] could not send ProbeResponse packet: %d\n",
           bss_->bssid().ToString().c_str(), status);
  }
}

}  // namespace wlan
