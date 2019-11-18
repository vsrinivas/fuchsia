// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/time.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <cinttypes>
#include <memory>
#include <utility>

#include <ddk/hw/wlan/wlaninfo.h>
#include <wlan/common/arraysize.h>
#include <wlan/common/buffer_writer.h>
#include <wlan/common/channel.h>
#include <wlan/common/logging.h>
#include <wlan/common/write_element.h>
#include <wlan/mlme/assoc_context.h>
#include <wlan/mlme/client/scanner.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/rates_elements.h>
#include <wlan/mlme/service.h>
#include <wlan/mlme/timer.h>
#include <wlan/mlme/wlan.h>

#include "lib/fidl/cpp/vector.h"

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

static constexpr size_t kMaxBss = 1000;

static void SendScanEnd(DeviceInterface* device, uint64_t txn_id, wlan_mlme::ScanResultCodes code) {
  wlan_mlme::ScanEnd msg;
  msg.txn_id = txn_id;
  msg.code = code;
  zx_status_t s =
      SendServiceMsg(device, &msg, fuchsia::wlan::mlme::internal::kMLME_OnScanEnd_GenOrdinal);
  if (s != ZX_OK) {
    errorf("failed to send OnScanEnd event: %d\n", s);
  }
}

static zx_status_t SendResults(DeviceInterface* device, uint64_t txn_id,
                               const std::unordered_map<uint64_t, Bss>& bss_map) {
  for (auto& p : bss_map) {
    wlan_mlme::ScanResult r;
    r.txn_id = txn_id;
    if (p.second.bss_desc().Clone(&r.bss) != ZX_OK) {
      continue;
    }
    zx_status_t status =
        SendServiceMsg(device, &r, fuchsia::wlan::mlme::internal::kMLME_OnScanResult_GenOrdinal);
    if (status != ZX_OK) {
      return status;
    }
  }
  return ZX_OK;
}

// TODO(NET-500): The way we handle Beacons and ProbeResponses in here is kinda
// gross. Refactor.

Scanner::Scanner(DeviceInterface* device, ChannelScheduler* chan_sched,
                 TimerManager<TimeoutTarget>* timer_mgr)
    : off_channel_handler_(this),
      device_(device),
      chan_sched_(chan_sched),
      timer_mgr_(timer_mgr),
      seq_mgr_(NewSequenceManager()) {}

zx_status_t Scanner::HandleMlmeScanReq(const MlmeMsg<wlan_mlme::ScanRequest>& req) {
  return Start(req);
}

zx_status_t Scanner::Start(const MlmeMsg<wlan_mlme::ScanRequest>& req) {
  debugfn();

  if (IsRunning()) {
    SendScanEnd(device_, req.body()->txn_id, wlan_mlme::ScanResultCodes::NOT_SUPPORTED);
    return ZX_ERR_UNAVAILABLE;
  }

  if (req.body()->channel_list->size() == 0 ||
      req.body()->max_channel_time < req.body()->min_channel_time) {
    SendScanEnd(device_, req.body()->txn_id, wlan_mlme::ScanResultCodes::INVALID_ARGS);
    return ZX_ERR_INVALID_ARGS;
  }
  // TODO(NET-629): re-enable checking the enum value after fidl2 lands
  // if (!BSSTypes_IsValidValue(req.body()->bss_type) ||
  // !ScanTypes_IsValidValue(req.body()->scan_type)) {
  //    return SendScanConfirm();
  //}

  req_ = wlan_mlme::ScanRequest::New();
  zx_status_t status = req.body()->Clone(req_.get());
  if (status != ZX_OK) {
    errorf("could not clone Scanrequest: %d\n", status);
    SendScanEnd(device_, req.body()->txn_id, wlan_mlme::ScanResultCodes::INTERNAL_ERROR);
    Reset();
    return status;
  }

  if (device_->GetWlanInfo().ifc_info.driver_features & WLAN_INFO_DRIVER_FEATURE_SCAN_OFFLOAD) {
    debugscan("starting a hardware scan\n");
    return StartHwScan();
  } else {
    debugscan("starting a software scan\n");
    ZX_DEBUG_ASSERT(channel_index_ == 0);
    chan_sched_->RequestOffChannelTime(CreateOffChannelRequest());
    return ZX_OK;
  }
}

zx_status_t Scanner::StartHwScan() {
  wlan_hw_scan_config_t config = {};
  if (req_->scan_type == wlan_mlme::ScanTypes::ACTIVE) {
    config.scan_type = WLAN_HW_SCAN_TYPE_ACTIVE;
  } else {
    config.scan_type = WLAN_HW_SCAN_TYPE_PASSIVE;
  }

  const auto& chans = req_->channel_list;
  if (chans->size() > arraysize(config.channels)) {
    errorf("too many channels to scan: %zu\n", chans->size());
    SendScanEnd(device_, req_->txn_id, wlan_mlme::ScanResultCodes::INVALID_ARGS);
    Reset();
    return ZX_ERR_INVALID_ARGS;
  }
  config.num_channels = chans->size();
  std::copy(chans->begin(), chans->end(), config.channels);

  if (req_->ssid.size() > arraysize(config.ssid.ssid)) {
    errorf("SSID too large: %zu\n", req_->ssid.size());
    SendScanEnd(device_, req_->txn_id, wlan_mlme::ScanResultCodes::INVALID_ARGS);
    Reset();
    return ZX_ERR_INVALID_ARGS;
  }
  config.ssid.len = static_cast<uint8_t>(req_->ssid.size());
  std::copy(req_->ssid.begin(), req_->ssid.end(), config.ssid.ssid);

  zx_status_t status = device_->StartHwScan(&config);
  if (status != ZX_OK) {
    errorf("StartHwScan returned an error: %s\n", zx_status_get_string(status));
    SendScanEnd(device_, req_->txn_id, wlan_mlme::ScanResultCodes::INTERNAL_ERROR);
    Reset();
    return status;
  }
  return ZX_OK;
}

void Scanner::OffChannelHandlerImpl::BeginOffChannelTime() {
  if (scanner_->req_->scan_type == wlan_mlme::ScanTypes::ACTIVE) {
    if (scanner_->req_->probe_delay == 0) {
      scanner_->SendProbeRequest(scanner_->ScanChannel());
    } else {
      scanner_->CancelTimeout();
      auto deadline = scanner_->timer_mgr_->Now() + WLAN_TU(scanner_->req_->probe_delay);
      scanner_->ScheduleTimeout(deadline);
    }
  }
}

void Scanner::OffChannelHandlerImpl::HandleOffChannelFrame(std::unique_ptr<Packet> pkt) {
  if (auto mgmt_frame = MgmtFrameView<>::CheckType(pkt.get()).CheckLength()) {
    if (auto bcn_frame = mgmt_frame.CheckBodyType<Beacon>().CheckLength()) {
      scanner_->HandleBeacon(bcn_frame);
    } else if (auto probe_frame = mgmt_frame.CheckBodyType<ProbeResponse>().CheckLength()) {
      scanner_->HandleProbeResponse(probe_frame);
    }
  }
}

bool Scanner::OffChannelHandlerImpl::EndOffChannelTime(bool interrupted,
                                                       OffChannelRequest* next_req) {
  scanner_->CancelTimeout();

  // If we were interrupted before the timeout ended, scan the channel again
  if (interrupted) {
    *next_req = scanner_->CreateOffChannelRequest();
    return true;
  }

  scanner_->channel_index_ += 1;
  if (scanner_->channel_index_ >= scanner_->req_->channel_list->size()) {
    scanner_->SendResultsAndReset();
    return false;
  }

  *next_req = scanner_->CreateOffChannelRequest();
  return true;
}

void Scanner::Reset() {
  debugfn();
  req_.reset();
  channel_index_ = 0;
  current_bss_.clear();
}

bool Scanner::IsRunning() const { return req_ != nullptr; }

wlan_channel_t Scanner::ScanChannel() const {
  debugfn();
  ZX_DEBUG_ASSERT(IsRunning());
  ZX_DEBUG_ASSERT(channel_index_ < req_->channel_list->size());
  return wlan_channel_t{
      .primary = req_->channel_list->at(channel_index_),
  };
}

void Scanner::ScheduleTimeout(zx::time deadline) {
  timer_mgr_->Schedule(deadline, TimeoutTarget::kScanner, &timeout_);
}

void Scanner::HandleTimeout() { SendProbeRequest(ScanChannel()); }

void Scanner::CancelTimeout() { timer_mgr_->Cancel(timeout_); }

bool Scanner::ShouldDropMgmtFrame(const MgmtFrameHeader& hdr) {
  // Ignore all management frames when scanner is not running.
  if (!IsRunning()) {
    return true;
  }

  common::MacAddr bssid(hdr.addr3);
  common::MacAddr src_addr(hdr.addr2);
  if (bssid != src_addr) {
    // Undefined situation. Investigate if roaming needs this or this is a plain
    // dark art. Do not process frame.
    debugbcn(
        "Rxed a beacon/probe_resp from the non-BSSID station: BSSID %s   "
        "SrcAddr %s\n",
        MACSTR(bssid), MACSTR(src_addr));
    return true;
  }

  return false;
}

void Scanner::HandleBeacon(const MgmtFrameView<Beacon>& frame) {
  debugfn();
  if (!ShouldDropMgmtFrame(*frame.hdr())) {
    auto bssid = frame.hdr()->addr3;
    auto rx_info = frame.rx_info();
    auto bcn_frame = frame.NextFrame();
    fbl::Span<const uint8_t> ie_chain = bcn_frame.body_data();

    ProcessBeaconOrProbeResponse(bssid, *bcn_frame.hdr(), ie_chain, rx_info);
  }
}

void Scanner::HandleProbeResponse(const MgmtFrameView<ProbeResponse>& frame) {
  debugfn();
  if (!ShouldDropMgmtFrame(*frame.hdr())) {
    auto bssid = frame.hdr()->addr3;
    auto rx_info = frame.rx_info();
    auto probe_resp_body = frame.NextFrame();
    fbl::Span<const uint8_t> ie_chain = probe_resp_body.body_data();

    Beacon beacon;
    beacon.timestamp = probe_resp_body.hdr()->timestamp;
    beacon.beacon_interval = probe_resp_body.hdr()->beacon_interval;
    beacon.cap = probe_resp_body.hdr()->cap;

    ProcessBeaconOrProbeResponse(bssid, beacon, ie_chain, rx_info);
  }
}

void Scanner::ProcessBeaconOrProbeResponse(const common::MacAddr bssid, const Beacon& beacon,
                                           fbl::Span<const uint8_t> ie_chain,
                                           const wlan_rx_info_t* rx_info) {
  debugfn();

  auto it = current_bss_.find(bssid.ToU64());
  if (it == current_bss_.end()) {
    if (current_bss_.size() >= kMaxBss) {
      errorf("maximum number of BSS reached: %lu\n", current_bss_.size());
      return;
    }
    it = current_bss_
             .emplace(std::piecewise_construct, std::forward_as_tuple(bssid.ToU64()),
                      std::forward_as_tuple(bssid))
             .first;
  }

  zx_status_t status = it->second.ProcessBeacon(beacon, ie_chain, rx_info);
  if (status != ZX_OK) {
    debugbcn("Failed to handle beacon (err %3d): BSSID %s timestamp: %15" PRIu64 "\n", status,
             MACSTR(bssid), beacon.timestamp);
  }
}

void Scanner::SendProbeRequest(wlan_channel_t channel) {
  debugfn();

  constexpr size_t reserved_ie_len = 256;
  constexpr size_t max_frame_len =
      MgmtFrameHeader::max_len() + ProbeRequest::max_len() + reserved_ie_len;
  auto packet = GetWlanPacket(max_frame_len);
  if (packet == nullptr) {
    errorf("scanner: error allocating buffer for ProbeRequest\n");
    return;
  }

  BufferWriter w(*packet);
  auto mgmt_hdr = w.Write<MgmtFrameHeader>();
  mgmt_hdr->fc.set_type(FrameType::kManagement);
  mgmt_hdr->fc.set_subtype(ManagementSubtype::kProbeRequest);
  uint32_t seq = mlme_sequence_manager_next_sns1(seq_mgr_.get(), &mgmt_hdr->addr1.byte);
  mgmt_hdr->sc.set_seq(seq);

  const common::MacAddr& mymac = device_->GetState()->address();
  const common::MacAddr& bssid = common::MacAddr(req_->bssid.data());
  mgmt_hdr->addr1 = common::kBcastMac;
  mgmt_hdr->addr2 = mymac;
  mgmt_hdr->addr3 = bssid;

  BufferWriter elem_w(w.RemainingBuffer());
  common::WriteSsid(&elem_w, {req_->ssid.data(), req_->ssid.size()});

  auto band_info = FindBand(device_->GetWlanInfo().ifc_info, common::Is5Ghz(channel));
  ZX_DEBUG_ASSERT(band_info != nullptr);
  if (band_info) {
    SupportedRate rates[WLAN_INFO_BAND_INFO_MAX_RATES];
    uint8_t num_rates = 0;
    for (uint8_t rate : band_info->rates) {
      if (rate == 0) {
        break;
      }
      rates[num_rates] = SupportedRate(band_info->rates[num_rates]);
      num_rates++;
    }
    ZX_DEBUG_ASSERT(num_rates > 0);
    RatesWriter rates_writer({rates, num_rates});
    rates_writer.WriteSupportedRates(&elem_w);
    rates_writer.WriteExtendedSupportedRates(&elem_w);
  } else {
    warnf("scanner: no rates found for chan %u; skip sending ProbeRequest\n", channel.primary);
    return;
  }

  packet->set_len(w.WrittenBytes() + elem_w.WrittenBytes());
  device_->SendWlan(std::move(packet));
}

OffChannelRequest Scanner::CreateOffChannelRequest() {
  return OffChannelRequest{.chan = ScanChannel(),
                           .duration = WLAN_TU(req_->max_channel_time),
                           .handler = &off_channel_handler_};
}

void Scanner::HandleHwScanAborted() {
  if (!IsRunning()) {
    errorf("got a HwScanAborted event while the scanner is not running\n");
    return;
  }
  errorf("scanner: hardware scan was aborted. Throwing out %zu BSS descriptions\n",
         current_bss_.size());
  SendScanEnd(device_, req_->txn_id, wlan_mlme::ScanResultCodes::INTERNAL_ERROR);
  Reset();
}

void Scanner::HandleHwScanComplete() {
  if (!IsRunning()) {
    errorf("got a HwScanComplete event while the scanner is not running\n");
    return;
  }
  SendResultsAndReset();
}

void Scanner::SendResultsAndReset() {
  zx_status_t status = SendResults(device_, req_->txn_id, current_bss_);
  if (status == ZX_OK) {
    SendScanEnd(device_, req_->txn_id, wlan_mlme::ScanResultCodes::SUCCESS);
  } else {
    errorf("scanner: failed to send results: %s\n", zx_status_get_string(status));
    SendScanEnd(device_, req_->txn_id, wlan_mlme::ScanResultCodes::INTERNAL_ERROR);
  }
  Reset();
}

}  // namespace wlan
