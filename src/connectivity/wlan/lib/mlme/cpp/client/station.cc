// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>

#include <src/connectivity/wlan/lib/mlme/rust/c-binding/bindings.h>
#include <wlan/common/band.h>
#include <wlan/common/buffer_writer.h>
#include <wlan/common/channel.h>
#include <wlan/common/element.h>
#include <wlan/common/energy.h>
#include <wlan/common/logging.h>
#include <wlan/common/parse_element.h>
#include <wlan/common/stats.h>
#include <wlan/common/tim_element.h>
#include <wlan/common/write_element.h>
#include <wlan/mlme/client/bss.h>
#include <wlan/mlme/client/client_mlme.h>
#include <wlan/mlme/client/station.h>
#include <wlan/mlme/debug.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/key.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/rates_elements.h>
#include <wlan/mlme/service.h>

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;
namespace wlan_stats = ::fuchsia::wlan::stats;
using common::dBm;

// TODO(hahnr): Revisit frame construction to reduce boilerplate code.

#define STA(c) static_cast<Station*>(c)
Station::Station(DeviceInterface* device, wlan_client_mlme_config_t* mlme_config,
                 TimerManager<TimeoutTarget>* timer_mgr, ChannelScheduler* chan_sched,
                 JoinContext* join_ctx)
    : device_(device),
      mlme_config_(mlme_config),
      rust_client_(nullptr, client_sta_delete),
      timer_mgr_(timer_mgr),
      chan_sched_(chan_sched),
      join_ctx_(join_ctx) {
  auto rust_device = mlme_device_ops_t{
      .device = static_cast<void*>(this),
      .deliver_eth_frame = [](void* sta, const uint8_t* data, size_t len) -> zx_status_t {
        return STA(sta)->device_->DeliverEthernet({data, len});
      },
      .send_wlan_frame = [](void* sta, mlme_out_buf_t buf, uint32_t flags) -> zx_status_t {
        auto pkt = FromRustOutBuf(buf);
        if (MgmtFrameView<>::CheckType(pkt.get())) {
          return STA(sta)->SendMgmtFrame(std::move(pkt));
        } else if (DataFrameView<>::CheckType(pkt.get())) {
          return STA(sta)->SendDataFrame(std::move(pkt), flags);
        }
        return STA(sta)->SendCtrlFrame(std::move(pkt));
      },
      .get_sme_channel = [](void* sta) -> zx_handle_t {
        return STA(sta)->device_->GetSmeChannelRef();
      },
      .set_wlan_channel = [](void* sta, wlan_channel_t chan) -> zx_status_t {
        return STA(sta)->device_->SetChannel(chan);
      },
      .get_wlan_channel = [](void* sta) -> wlan_channel_t {
        return STA(sta)->device_->GetState()->channel();
      },
      .set_key = [](void* sta, wlan_key_config_t* key) -> zx_status_t {
        return STA(sta)->device_->SetKey(key);
      },
      .configure_bss = [](void* sta, wlan_bss_config_t* cfg) -> zx_status_t {
        return STA(sta)->device_->ConfigureBss(cfg);
      },
      .enable_beaconing = [](void* sta, const uint8_t* beacon_tmpl_data, size_t beacon_tmpl_len,
                             size_t tim_ele_offset, uint16_t beacon_interval) -> zx_status_t {
        // The client never needs to enable beaconing.
        return ZX_ERR_NOT_SUPPORTED;
      },
      .disable_beaconing = [](void* sta) -> zx_status_t {
        // The client never needs to disable beaconing.
        return ZX_ERR_NOT_SUPPORTED;
      },
  };
  wlan_scheduler_ops_t scheduler = {
      .cookie = static_cast<void*>(this),
      .now = [](void* cookie) -> zx_time_t { return STA(cookie)->timer_mgr_->Now().get(); },
      .schedule = [](void* cookie, int64_t deadline) -> wlan_scheduler_event_id_t {
        TimeoutId id = {};
        STA(cookie)->timer_mgr_->Schedule(zx::time(deadline), TimeoutTarget::kRust, &id);
        return {._0 = id.raw()};
      },
      .cancel =
          [](void* cookie, wlan_scheduler_event_id_t id) {
            STA(cookie)->timer_mgr_->Cancel(TimeoutId(id._0));
          },
  };
  rust_client_ = NewClientStation(rust_device, rust_buffer_provider, scheduler, join_ctx_->bssid(),
                                  self_addr());
  Reset();
}
#undef STA

void Station::Reset() {
  debugfn();

  state_ = WlanState::kIdle;
  timer_mgr_->CancelAll();
}

zx_status_t Station::HandleWlanFrame(std::unique_ptr<Packet> pkt) {
  ZX_DEBUG_ASSERT(pkt->peer() == Packet::Peer::kWlan);
  WLAN_STATS_INC(rx_frame.in);
  WLAN_STATS_ADD(pkt->len(), rx_frame.in_bytes);

  if (auto possible_mgmt_frame = MgmtFrameView<>::CheckType(pkt.get())) {
    auto mgmt_frame = possible_mgmt_frame.CheckLength();
    if (!mgmt_frame) {
      return ZX_ERR_BUFFER_TOO_SMALL;
    }

    HandleMgmtFrame(mgmt_frame.IntoOwned(std::move(pkt)));
  } else if (auto possible_data_frame = DataFrameView<>::CheckType(pkt.get())) {
    auto data_frame = possible_data_frame.CheckLength();
    if (!data_frame) {
      return ZX_ERR_BUFFER_TOO_SMALL;
    }

    HandleDataFrame(data_frame.IntoOwned(std::move(pkt)));
  }

  return ZX_OK;
}

zx_status_t Station::HandleMgmtFrame(MgmtFrame<>&& frame) {
  auto mgmt_frame = frame.View();

  WLAN_STATS_INC(mgmt_frame.in);
  if (ShouldDropMgmtFrame(mgmt_frame)) {
    WLAN_STATS_INC(mgmt_frame.drop);
    return ZX_ERR_NOT_SUPPORTED;
  }
  WLAN_STATS_INC(mgmt_frame.out);

  if (auto possible_bcn_frame = mgmt_frame.CheckBodyType<Beacon>()) {
    if (auto bcn_frame = possible_bcn_frame.CheckLength()) {
      HandleBeacon(bcn_frame.IntoOwned(frame.Take()));
    }
  } else if (auto possible_auth_frame = mgmt_frame.CheckBodyType<Authentication>()) {
    if (auto auth_frame = possible_auth_frame.CheckLength()) {
      HandleAuthentication(auth_frame.IntoOwned(frame.Take()));
    }
  } else if (auto possible_deauth_frame = mgmt_frame.CheckBodyType<Deauthentication>()) {
    if (auto deauth_frame = possible_deauth_frame.CheckLength()) {
      HandleDeauthentication(deauth_frame.IntoOwned(frame.Take()));
    }
  } else if (auto possible_assoc_resp_frame = mgmt_frame.CheckBodyType<AssociationResponse>()) {
    if (auto assoc_resp_frame = possible_assoc_resp_frame.CheckLength()) {
      HandleAssociationResponse(assoc_resp_frame.IntoOwned(frame.Take()));
    }
  } else if (auto possible_disassoc_frame = mgmt_frame.CheckBodyType<Disassociation>()) {
    if (auto disassoc_frame = possible_disassoc_frame.CheckLength()) {
      HandleDisassociation(disassoc_frame.IntoOwned(frame.Take()));
    }
  } else if (auto possible_action_frame = mgmt_frame.CheckBodyType<ActionFrame>()) {
    if (auto action_frame = possible_action_frame.CheckLength()) {
      HandleActionFrame(action_frame.IntoOwned(frame.Take()));
    }
  }

  return ZX_OK;
}

zx_status_t Station::HandleDataFrame(DataFrame<>&& frame) {
  auto data_frame = frame.View();

  WLAN_STATS_INC(data_frame.in);
  if (ShouldDropDataFrame(data_frame)) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  ZX_DEBUG_ASSERT(state_ == WlanState::kAssociated);

  auto rx_info = frame.View().rx_info();
  if (rx_info->valid_fields & WLAN_RX_INFO_VALID_DATA_RATE) {
    auto rssi_dbm = rx_info->rssi_dbm;
    WLAN_RSSI_HIST_INC(assoc_data_rssi, rssi_dbm);
    // Take signal strength into account.
    avg_rssi_dbm_.add(dBm(rssi_dbm));
  }

  // Ignore "more_data" bit if RSNA was not yet established.
  if (controlled_port_ == eapol::PortState::kOpen) {
    // PS-POLL if there are more buffered unicast frames.
    auto data_hdr = frame.View().hdr();
    if (data_hdr->fc.more_data() && data_hdr->addr1.IsUcast()) {
      SendPsPoll();
    }
  }

  auto pkt = frame.Take();
  const bool has_padding =
      rx_info != nullptr && rx_info->rx_flags & WLAN_RX_INFO_FLAGS_FRAME_BODY_PADDING_4;
  bool is_controlled_port_open = controlled_port_ == eapol::PortState::kOpen;
  auto frame_span = fbl::Span<uint8_t>{pkt->data(), pkt->len()};
  client_sta_handle_data_frame(rust_client_.get(), AsWlanSpan(frame_span), has_padding,
                               is_controlled_port_open);

  return ZX_OK;
}

zx_status_t Station::Authenticate(wlan_mlme::AuthenticationTypes auth_type, uint32_t timeout) {
  debugfn();
  WLAN_STATS_INC(svc_msg.in);

  if (state_ != WlanState::kIdle) {
    errorf("received AUTHENTICATE.request in unexpected state: %u\n", state_);
    return service::SendAuthConfirm(device_, join_ctx_->bssid(),
                                    wlan_mlme::AuthenticateResultCodes::REFUSED);
  }

  if (auth_type != wlan_mlme::AuthenticationTypes::OPEN_SYSTEM) {
    errorf("only OpenSystem authentication is supported\n");
    return service::SendAuthConfirm(device_, join_ctx_->bssid(),
                                    wlan_mlme::AuthenticateResultCodes::REFUSED);
  }

  debugjoin("authenticating to %s\n", join_ctx_->bssid().ToString().c_str());

  zx::time deadline = deadline_after_bcn_period(timeout);
  zx_status_t status = timer_mgr_->Schedule(deadline, {}, &auth_timeout_);
  if (status != ZX_OK) {
    errorf("could not set authentication timeout event: %s\n", zx_status_get_string(status));
    // This is the wrong result code, but we need to define our own codes at
    // some later time.
    service::SendAuthConfirm(device_, join_ctx_->bssid(),
                             wlan_mlme::AuthenticateResultCodes::REFUSED);
    return status;
  }

  status = client_sta_send_open_auth_frame(rust_client_.get());
  if (status != ZX_OK) {
    errorf("could not send open auth frame: %d\n", status);
    timer_mgr_->Cancel(auth_timeout_);
    service::SendAuthConfirm(device_, join_ctx_->bssid(),
                             wlan_mlme::AuthenticateResultCodes::REFUSED);
    return status;
  }

  state_ = WlanState::kAuthenticating;
  return status;
}

zx_status_t Station::Deauthenticate(wlan_mlme::ReasonCode reason_code) {
  debugfn();
  WLAN_STATS_INC(svc_msg.in);

  if (state_ != WlanState::kAssociated && state_ != WlanState::kAuthenticated) {
    errorf("not associated or authenticated; ignoring deauthenticate request\n");
    return ZX_OK;
  }

  client_sta_send_deauth_frame(rust_client_.get(), static_cast<uint16_t>(reason_code));
  infof("deauthenticating from \"%s\" (%s), reason=%hu\n",
        debug::ToAsciiOrHexStr(join_ctx_->bss()->ssid).c_str(),
        join_ctx_->bssid().ToString().c_str(), reason_code);

  if (state_ == WlanState::kAssociated) {
    device_->ClearAssoc(join_ctx_->bssid());
  }
  state_ = WlanState::kIdle;
  device_->SetStatus(0);
  controlled_port_ = eapol::PortState::kBlocked;
  service::SendDeauthConfirm(device_, join_ctx_->bssid());

  return ZX_OK;
}

zx_status_t Station::Associate(const wlan_mlme::AssociateRequest& req) {
  debugfn();
  WLAN_STATS_INC(svc_msg.in);

  if (state_ != WlanState::kAuthenticated) {
    if (state_ == WlanState::kAssociated) {
      warnf("already associated; sending request anyway\n");
    } else {
      // TODO(tkilbourn): better result codes
      errorf("must authenticate before associating\n");
      return service::SendAuthConfirm(device_, join_ctx_->bssid(),
                                      wlan_mlme::AuthenticateResultCodes::REFUSED);
    }
  }

  fbl::Span<const uint8_t> rsne{};
  if (req.rsne.has_value()) {
    rsne = req.rsne.value();
  }

  fbl::Span<const uint8_t> ht_cap{};
  if (req.ht_cap != nullptr) {
    ht_cap = req.ht_cap->bytes;
  }

  fbl::Span<const uint8_t> vht_cap{};
  if (req.vht_cap != nullptr) {
    vht_cap = req.vht_cap->bytes;
  }

  zx_status_t status = client_sta_send_assoc_req_frame(
      rust_client_.get(), req.cap_info, AsWlanSpan({join_ctx_->bss()->ssid}),
      AsWlanSpan({req.rates}), AsWlanSpan(rsne), AsWlanSpan(ht_cap), AsWlanSpan(vht_cap));
  if (status != ZX_OK) {
    errorf("could not send assoc packet: %d\n", status);
    service::SendAssocConfirm(device_, wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
    return status;
  }

  // TODO(NET-500): Add association timeout to MLME-ASSOCIATE.request just like
  // JOIN and AUTHENTICATE requests do.
  zx::time deadline = deadline_after_bcn_period(kAssocBcnCountTimeout);
  status = timer_mgr_->Schedule(deadline, {}, &assoc_timeout_);
  if (status != ZX_OK) {
    errorf("could not set auth timedout event: %d\n", status);
    // This is the wrong result code, but we need to define our own codes at
    // some later time.
    service::SendAssocConfirm(device_, wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
    // TODO(tkilbourn): reset the station?
  }
  return status;
}

bool Station::ShouldDropMgmtFrame(const MgmtFrameView<>& frame) {
  // Drop management frames if either, there is no BSSID set yet,
  // or the frame is not from the BSS.
  return join_ctx_->bssid() != frame.hdr()->addr3;
}

// TODO(NET-500): Using a single method for joining and associated state is not
// ideal. The logic should be split up and decided on a higher level based on
// the current state.
void Station::HandleBeacon(MgmtFrame<Beacon>&& frame) {
  debugfn();

  if (frame.View().rx_info()->valid_fields & WLAN_RX_INFO_VALID_DATA_RATE) {
    auto rssi_dbm = frame.View().rx_info()->rssi_dbm;
    avg_rssi_dbm_.add(dBm(rssi_dbm));
    WLAN_RSSI_HIST_INC(beacon_rssi, rssi_dbm);
  }

  if (state_ != WlanState::kAssociated) {
    return;
  }

  remaining_auto_deauth_timeout_ = FullAutoDeauthDuration();
  auto_deauth_last_accounted_ = timer_mgr_->Now();

  auto bcn_frame = frame.View().NextFrame();
  fbl::Span<const uint8_t> ie_chain = bcn_frame.body_data();
  auto tim = common::FindAndParseTim(ie_chain);
  if (tim && common::IsTrafficBuffered(assoc_ctx_.aid, tim->header, tim->bitmap)) {
    SendPsPoll();
  }
}

zx_status_t Station::HandleAuthentication(MgmtFrame<Authentication>&& frame) {
  debugfn();

  if (state_ != WlanState::kAuthenticating) {
    debugjoin("unexpected authentication frame in state: %u; ignoring frame\n", state_);
    return ZX_OK;
  }

  // Authentication notification received. Cancel pending timeout.
  timer_mgr_->Cancel(auth_timeout_);

  auto auth_hdr = frame.body_data();
  zx_status_t status = mlme_is_valid_open_auth_resp(AsWlanSpan(auth_hdr));
  if (status == ZX_OK) {
    state_ = WlanState::kAuthenticated;
    debugjoin("authenticated to %s\n", join_ctx_->bssid().ToString().c_str());
    service::SendAuthConfirm(device_, join_ctx_->bssid(),
                             wlan_mlme::AuthenticateResultCodes::SUCCESS);
  } else {
    state_ = WlanState::kIdle;
    service::SendAuthConfirm(device_, join_ctx_->bssid(),
                             wlan_mlme::AuthenticateResultCodes::AUTHENTICATION_REJECTED);
  }
  return status;
}

zx_status_t Station::HandleDeauthentication(MgmtFrame<Deauthentication>&& frame) {
  debugfn();

  if (state_ != WlanState::kAssociated && state_ != WlanState::kAuthenticated) {
    debugjoin("got spurious deauthenticate; ignoring\n");
    return ZX_OK;
  }

  auto deauth = frame.body();
  infof("deauthenticating from \"%s\" (%s), reason=%hu\n",
        debug::ToAsciiOrHexStr(join_ctx_->bss()->ssid).c_str(),
        join_ctx_->bssid().ToString().c_str(), deauth->reason_code);

  if (state_ == WlanState::kAssociated) {
    device_->ClearAssoc(join_ctx_->bssid());
  }
  state_ = WlanState::kIdle;
  device_->SetStatus(0);
  controlled_port_ = eapol::PortState::kBlocked;

  return service::SendDeauthIndication(device_, join_ctx_->bssid(),
                                       static_cast<wlan_mlme::ReasonCode>(deauth->reason_code));
}

zx_status_t Station::HandleAssociationResponse(MgmtFrame<AssociationResponse>&& frame) {
  debugfn();

  if (state_ != WlanState::kAuthenticated) {
    // TODO(tkilbourn): should we process this Association response packet
    // anyway? The spec is unclear.
    debugjoin("unexpected association response frame\n");
    return ZX_OK;
  }

  // Receive association response, cancel association timeout.
  timer_mgr_->Cancel(assoc_timeout_);

  auto assoc = frame.body();
  if (assoc->status_code != WLAN_STATUS_CODE_SUCCESS) {
    errorf("association failed (status code=%u)\n", assoc->status_code);
    // TODO(tkilbourn): map to the correct result code
    service::SendAssocConfirm(device_, wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
    return ZX_ERR_BAD_STATE;
  }

  auto status = SetAssocContext(frame.View());
  if (status != ZX_OK) {
    errorf("failed to set association context (status %d)\n", status);
    service::SendAssocConfirm(device_, wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
    return ZX_ERR_BAD_STATE;
  }

  // TODO(porce): Move into |assoc_ctx_|
  state_ = WlanState::kAssociated;
  assoc_ctx_.aid = assoc->aid;

  // Spread the good news upward
  service::SendAssocConfirm(device_, wlan_mlme::AssociateResultCodes::SUCCESS, assoc_ctx_.aid);
  // Spread the good news downward
  NotifyAssocContext();

  // Initiate RSSI reporting to Wlanstack.
  zx::time deadline = deadline_after_bcn_period(mlme_config_->signal_report_beacon_timeout);
  timer_mgr_->Schedule(deadline, {}, &signal_report_timeout_);
  avg_rssi_dbm_.reset();
  if (frame.View().rx_info()->valid_fields & WLAN_RX_INFO_VALID_DATA_RATE) {
    avg_rssi_dbm_.add(dBm(frame.View().rx_info()->rssi_dbm));
    service::SendSignalReportIndication(device_, common::dBm(frame.View().rx_info()->rssi_dbm));
  }

  remaining_auto_deauth_timeout_ = FullAutoDeauthDuration();
  status = timer_mgr_->Schedule(timer_mgr_->Now() + remaining_auto_deauth_timeout_, {},
                                &auto_deauth_timeout_);
  if (status != ZX_OK) {
    warnf("could not set auto-deauthentication timeout event\n");
  }

  // Open port if user connected to an open network.
  if (!join_ctx_->bss()->rsne.has_value()) {
    debugjoin("802.1X controlled port is now open\n");
    controlled_port_ = eapol::PortState::kOpen;
    device_->SetStatus(ETHERNET_STATUS_ONLINE);
  }

  infof("NIC %s associated with \"%s\"(%s) in channel %s, %s, %s\n", self_addr().ToString().c_str(),
        debug::ToAsciiOrHexStr(join_ctx_->bss()->ssid).c_str(), assoc_ctx_.bssid.ToString().c_str(),
        common::ChanStrLong(assoc_ctx_.chan).c_str(), common::BandStr(assoc_ctx_.chan).c_str(),
        common::GetPhyStr(assoc_ctx_.phy).c_str());

  // TODO(porce): Time when to establish BlockAck session
  // Handle MLME-level retry, if MAC-level retry ultimately fails
  // Wrap this as EstablishBlockAckSession(peer_mac_addr)
  // Signal to lower MAC for proper session handling

  if (join_ctx_->IsHt() || join_ctx_->IsVht()) {
    SendAddBaRequestFrame();
  }
  return ZX_OK;
}

zx_status_t Station::HandleDisassociation(MgmtFrame<Disassociation>&& frame) {
  debugfn();

  if (state_ != WlanState::kAssociated) {
    debugjoin("got spurious disassociate; ignoring\n");
    return ZX_OK;
  }

  auto disassoc = frame.body();
  infof("disassociating from \"%s\"(%s), reason=%u\n",
        debug::ToAsciiOrHexStr(join_ctx_->bss()->ssid).c_str(),
        join_ctx_->bssid().ToString().c_str(), disassoc->reason_code);

  state_ = WlanState::kAuthenticated;
  device_->ClearAssoc(join_ctx_->bssid());
  device_->SetStatus(0);
  controlled_port_ = eapol::PortState::kBlocked;
  timer_mgr_->Cancel(signal_report_timeout_);

  return service::SendDisassociateIndication(device_, join_ctx_->bssid(), disassoc->reason_code);
}

zx_status_t Station::HandleActionFrame(MgmtFrame<ActionFrame>&& frame) {
  debugfn();

  auto action_frame = frame.View().NextFrame();
  if (auto action_ba_frame = action_frame.CheckBodyType<ActionFrameBlockAck>().CheckLength()) {
    auto ba_frame = action_ba_frame.NextFrame();
    if (auto _add_ba_resp_frame = ba_frame.CheckBodyType<AddBaResponseFrame>().CheckLength()) {
      // TODO(porce): Handle AddBaResponses and keep the result of negotiation.
    } else if (auto add_ba_req_frame = ba_frame.CheckBodyType<AddBaRequestFrame>().CheckLength()) {
      return HandleAddBaRequest(*add_ba_req_frame.body());
    }
  }

  return ZX_OK;
}

zx_status_t Station::HandleAddBaRequest(const AddBaRequestFrame& addbareq) {
  debugfn();

  constexpr size_t max_frame_len = MgmtFrameHeader::max_len() + ActionFrame::max_len() +
                                   ActionFrameBlockAck::max_len() + AddBaRequestFrame::max_len();
  auto packet = GetWlanPacket(max_frame_len);
  if (packet == nullptr) {
    return ZX_ERR_NO_RESOURCES;
  }

  BufferWriter w(*packet);
  auto mgmt_hdr = w.Write<MgmtFrameHeader>();
  mgmt_hdr->fc.set_type(FrameType::kManagement);
  mgmt_hdr->fc.set_subtype(ManagementSubtype::kAction);
  mgmt_hdr->addr1 = join_ctx_->bssid();
  mgmt_hdr->addr2 = self_addr();
  mgmt_hdr->addr3 = join_ctx_->bssid();
  auto seq_mgr = client_sta_seq_mgr(rust_client_.get());
  auto seq_num = mlme_sequence_manager_next_sns1(seq_mgr, &mgmt_hdr->addr1.byte);
  mgmt_hdr->sc.set_seq(seq_num);

  w.Write<ActionFrame>()->category = ActionFrameBlockAck::ActionCategory();
  w.Write<ActionFrameBlockAck>()->action = AddBaResponseFrame::BlockAckAction();

  auto addbaresp_hdr = w.Write<AddBaResponseFrame>();
  addbaresp_hdr->dialog_token = addbareq.dialog_token;

  // TODO(porce): Implement DelBa as a response to AddBar for decline

  // Note: Returning AddBaResponse with status_code::kRefused seems ineffective.
  // ArubaAP is persistent not honoring that.
  addbaresp_hdr->status_code = WLAN_STATUS_CODE_SUCCESS;

  addbaresp_hdr->params.set_amsdu(addbareq.params.amsdu() == 1);
  addbaresp_hdr->params.set_policy(BlockAckParameters::kImmediate);
  addbaresp_hdr->params.set_tid(addbareq.params.tid());

  // TODO(NET-500): Is this Ralink specific?
  // TODO(porce): Once chipset capability is ready, refactor below buffer_size
  // calculation.
  size_t buffer_size_ap = addbareq.params.buffer_size();
  constexpr size_t buffer_size_ralink = 64;
  size_t buffer_size = (buffer_size_ap <= buffer_size_ralink) ? buffer_size_ap : buffer_size_ralink;
  addbaresp_hdr->params.set_buffer_size(buffer_size);
  addbaresp_hdr->timeout = addbareq.timeout;

  packet->set_len(w.WrittenBytes());

  auto status = SendMgmtFrame(std::move(packet));
  if (status != ZX_OK) {
    errorf("could not send AddBaResponse: %d\n", status);
  }
  return status;
}

bool Station::ShouldDropDataFrame(const DataFrameView<>& frame) {
  if (state_ != WlanState::kAssociated) {
    return true;
  }

  return join_ctx_->bssid() != frame.hdr()->addr2;
}

zx_status_t Station::HandleEthFrame(EthFrame&& eth_frame) {
  debugfn();
  if (state_ != WlanState::kAssociated) {
    debugf("dropping eth packet while not associated\n");
    return ZX_ERR_BAD_STATE;
  }

  // If off channel, drop the frame and let upper layer handle retransmission if
  // necessary.
  if (!chan_sched_->OnChannel()) {
    return ZX_ERR_IO_NOT_PRESENT;
  }

  bool needs_protection =
      join_ctx_->bss()->rsne.has_value() && controlled_port_ == eapol::PortState::kOpen;
  auto eth_hdr = eth_frame.hdr();
  return client_sta_send_data_frame(rust_client_.get(), &eth_hdr->src.byte, &eth_hdr->dest.byte,
                                    needs_protection, IsQosReady(), eth_hdr->ether_type(),
                                    AsWlanSpan(eth_frame.body_data()));
}

void Station::HandleTimeout(zx::time now, TimeoutTarget target, TimeoutId timeout_id) {
  debugfn();

  if (target == TimeoutTarget::kRust) {
    client_sta_timeout_fired(rust_client_.get(), wlan_scheduler_event_id_t{._0 = timeout_id.raw()});
    return;
  }

  ZX_ASSERT(target == TimeoutTarget::kDefault);

  if (timeout_id == auth_timeout_) {
    debugjoin("auth timed out; moving back to idle state\n");
    state_ = WlanState::kIdle;
    service::SendAuthConfirm(device_, join_ctx_->bssid(),
                             wlan_mlme::AuthenticateResultCodes::AUTH_FAILURE_TIMEOUT);
  } else if (timeout_id == assoc_timeout_) {
    debugjoin("assoc timed out; moving back to authenticated\n");
    // TODO(tkilbourn): need a better error code for this
    service::SendAssocConfirm(device_, wlan_mlme::AssociateResultCodes::REFUSED_TEMPORARILY);
  } else if (timeout_id == signal_report_timeout_) {
    if (state_ == WlanState::kAssociated) {
      service::SendSignalReportIndication(device_, common::to_dBm(avg_rssi_dbm_.avg()));

      zx::time deadline = deadline_after_bcn_period(mlme_config_->signal_report_beacon_timeout);
      timer_mgr_->Schedule(deadline, {}, &signal_report_timeout_);
    }
  } else if (timeout_id == auto_deauth_timeout_) {
    debugclt("now: %lu\n", now.get());
    debugclt("remaining auto-deauth timeout: %lu\n", remaining_auto_deauth_timeout_.get());
    debugclt("auto-deauth last accounted time: %lu\n", auto_deauth_last_accounted_.get());

    if (!chan_sched_->OnChannel()) {
      ZX_DEBUG_ASSERT("auto-deauth timeout should not trigger while off channel\n");
    } else if (remaining_auto_deauth_timeout_ > now - auto_deauth_last_accounted_) {
      // Update the remaining auto-deauth timeout with the unaccounted time
      remaining_auto_deauth_timeout_ -= now - auto_deauth_last_accounted_;
      auto_deauth_last_accounted_ = now;
      timer_mgr_->Schedule(now + remaining_auto_deauth_timeout_, {}, &auto_deauth_timeout_);
    } else if (state_ == WlanState::kAssociated) {
      infof("lost BSS; deauthenticating...\n");
      state_ = WlanState::kIdle;
      device_->ClearAssoc(join_ctx_->bssid());
      device_->SetStatus(0);
      controlled_port_ = eapol::PortState::kBlocked;

      auto reason_code = wlan_mlme::ReasonCode::LEAVING_NETWORK_DEAUTH;
      service::SendDeauthIndication(device_, join_ctx_->bssid(), reason_code);
      client_sta_send_deauth_frame(rust_client_.get(), static_cast<uint16_t>(reason_code));
    }
  }
}

zx_status_t Station::SendAddBaRequestFrame() {
  debugfn();

  if (state_ != WlanState::kAssociated) {
    errorf(
        "won't send ADDBA Request in other than Associated state. Current "
        "state: %d\n",
        state_);
    return ZX_ERR_BAD_STATE;
  }

  constexpr size_t max_frame_size = MgmtFrameHeader::max_len() + ActionFrame::max_len() +
                                    ActionFrameBlockAck::max_len() + AddBaRequestFrame::max_len();
  auto packet = GetWlanPacket(max_frame_size);
  if (packet == nullptr) {
    return ZX_ERR_NO_RESOURCES;
  }

  BufferWriter w(*packet);
  auto mgmt_hdr = w.Write<MgmtFrameHeader>();
  mgmt_hdr->fc.set_type(FrameType::kManagement);
  mgmt_hdr->fc.set_subtype(ManagementSubtype::kAction);
  mgmt_hdr->addr1 = join_ctx_->bssid();
  mgmt_hdr->addr2 = self_addr();
  mgmt_hdr->addr3 = join_ctx_->bssid();
  auto seq_mgr = client_sta_seq_mgr(rust_client_.get());
  auto seq_num = mlme_sequence_manager_next_sns1(seq_mgr, &mgmt_hdr->addr1.byte);
  mgmt_hdr->sc.set_seq(seq_num);

  auto action_hdr = w.Write<ActionFrame>();
  action_hdr->category = ActionFrameBlockAck::ActionCategory();

  auto ba_hdr = w.Write<ActionFrameBlockAck>();
  ba_hdr->action = AddBaRequestFrame::BlockAckAction();

  auto addbareq_hdr = w.Write<AddBaRequestFrame>();
  // It appears there is no particular rule to choose the value for
  // dialog_token. See IEEE Std 802.11-2016, 9.6.5.2.
  addbareq_hdr->dialog_token = 0x01;
  addbareq_hdr->params.set_amsdu(1);
  addbareq_hdr->params.set_policy(BlockAckParameters::BlockAckPolicy::kImmediate);
  addbareq_hdr->params.set_tid(GetTid());  // TODO(porce): Communicate this with lower MAC.
  // TODO(porce): Fix the discrepancy of this value from the Ralink's TXWI
  // ba_win_size setting
  addbareq_hdr->params.set_buffer_size(64);
  addbareq_hdr->timeout = 0;               // Disables the timeout
  addbareq_hdr->seq_ctrl.set_fragment(0);  // TODO(porce): Send this down to the lower MAC
  addbareq_hdr->seq_ctrl.set_starting_seq(1);

  packet->set_len(w.WrittenBytes());

  auto status = SendMgmtFrame(std::move(packet));
  if (status != ZX_OK) {
    errorf("could not send AddBaRequest: %d\n", status);
    return status;
  }

  return ZX_OK;
}

zx_status_t Station::SendEapolFrame(fbl::Span<const uint8_t> eapol_frame,
                                    const common::MacAddr& src, const common::MacAddr& dst) {
  debugfn();
  WLAN_STATS_INC(svc_msg.in);

  if (state_ != WlanState::kAssociated) {
    debugf(
        "dropping MLME-EAPOL.request while not being associated. STA in state "
        "%d\n",
        state_);
    return ZX_OK;
  }

  bool needs_protection =
      join_ctx_->bss()->rsne.has_value() && controlled_port_ == eapol::PortState::kOpen;
  client_sta_send_eapol_frame(rust_client_.get(), &src.byte, &dst.byte, needs_protection,
                              AsWlanSpan(eapol_frame));
  return ZX_OK;
}

zx_status_t Station::SetKeys(fbl::Span<const wlan_mlme::SetKeyDescriptor> keys) {
  debugfn();
  WLAN_STATS_INC(svc_msg.in);

  for (auto& keyDesc : keys) {
    auto key_config = ToKeyConfig(keyDesc);
    if (!key_config.has_value()) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    auto status = device_->SetKey(&key_config.value());
    if (status != ZX_OK) {
      errorf("Could not configure keys in hardware: %d\n", status);
      return status;
    }
  }

  return ZX_OK;
}

void Station::UpdateControlledPort(wlan_mlme::ControlledPortState state) {
  WLAN_STATS_INC(svc_msg.in);

  if (state == wlan_mlme::ControlledPortState::OPEN) {
    controlled_port_ = eapol::PortState::kOpen;
    device_->SetStatus(ETHERNET_STATUS_ONLINE);
  } else {
    controlled_port_ = eapol::PortState::kBlocked;
    device_->SetStatus(0);
  }
}

void Station::PreSwitchOffChannel() {
  debugfn();
  if (state_ == WlanState::kAssociated) {
    SetPowerManagementMode(true);

    timer_mgr_->Cancel(auto_deauth_timeout_);
    zx::duration unaccounted_time = timer_mgr_->Now() - auto_deauth_last_accounted_;
    if (remaining_auto_deauth_timeout_ > unaccounted_time) {
      remaining_auto_deauth_timeout_ -= unaccounted_time;
    } else {
      remaining_auto_deauth_timeout_ = zx::duration(0);
    }
  }
}

void Station::BackToMainChannel() {
  debugfn();
  if (state_ == WlanState::kAssociated) {
    SetPowerManagementMode(false);

    zx::time now = timer_mgr_->Now();
    auto deadline = now + std::max(remaining_auto_deauth_timeout_, WLAN_TU(1u));
    timer_mgr_->Schedule(deadline, {}, &auto_deauth_timeout_);
    auto_deauth_last_accounted_ = now;
  }
}

zx_status_t Station::SendCtrlFrame(std::unique_ptr<Packet> packet) {
  chan_sched_->EnsureOnChannel(timer_mgr_->Now() +
                               zx::duration(mlme_config_->ensure_on_channel_time));
  return SendWlan(std::move(packet));
}

zx_status_t Station::SendMgmtFrame(std::unique_ptr<Packet> packet) {
  chan_sched_->EnsureOnChannel(timer_mgr_->Now() +
                               zx::duration(mlme_config_->ensure_on_channel_time));
  return SendWlan(std::move(packet));
}

zx_status_t Station::SendDataFrame(std::unique_ptr<Packet> packet, uint32_t flags) {
  return SendWlan(std::move(packet), flags);
}

zx_status_t Station::SetPowerManagementMode(bool ps_mode) {
  if (state_ != WlanState::kAssociated) {
    warnf("cannot adjust power management before being associated\n");
    return ZX_OK;
  }

  auto packet = GetWlanPacket(DataFrameHeader::max_len());
  if (packet == nullptr) {
    return ZX_ERR_NO_RESOURCES;
  }

  BufferWriter w(*packet);
  auto data_hdr = w.Write<DataFrameHeader>();
  data_hdr->fc.set_type(FrameType::kData);
  data_hdr->fc.set_subtype(DataSubtype::kNull);
  data_hdr->fc.set_pwr_mgmt(ps_mode);
  data_hdr->fc.set_to_ds(1);
  data_hdr->addr1 = join_ctx_->bssid();
  data_hdr->addr2 = self_addr();
  data_hdr->addr3 = join_ctx_->bssid();
  auto seq_mgr = client_sta_seq_mgr(rust_client_.get());
  auto seq_num = mlme_sequence_manager_next_sns1(seq_mgr, &data_hdr->addr1.byte);
  data_hdr->sc.set_seq(seq_num);

  packet->set_len(w.WrittenBytes());
  auto status = SendDataFrame(std::move(packet));
  if (status != ZX_OK) {
    errorf("could not send power management frame to set to %d: %s\n", ps_mode,
           zx_status_get_string(status));
  }
  return status;
}

zx_status_t Station::SendPsPoll() {
  // TODO(hahnr): We should probably wait for an RSNA if the network is an
  // RSN. Else we cannot work with the incoming data frame.
  if (state_ != WlanState::kAssociated) {
    warnf("cannot send ps-poll before being associated\n");
    return ZX_OK;
  }

  auto status = client_sta_send_ps_poll_frame(rust_client_.get(), assoc_ctx_.aid);
  if (status != ZX_OK) {
    errorf("could not send power management packet: %d\n", status);
    return status;
  }
  return ZX_OK;
}

zx_status_t Station::SendWlan(std::unique_ptr<Packet> packet, uint32_t flags) {
  auto packet_bytes = packet->len();
  zx_status_t status = device_->SendWlan(std::move(packet), flags);
  if (status == ZX_OK) {
    WLAN_STATS_INC(tx_frame.out);
    WLAN_STATS_ADD(packet_bytes, tx_frame.out_bytes);
  }
  return status;
}

zx::time Station::deadline_after_bcn_period(size_t bcn_count) {
  return timer_mgr_->Now() + WLAN_TU(join_ctx_->bss()->beacon_period * bcn_count);
}

zx::duration Station::FullAutoDeauthDuration() {
  return WLAN_TU(join_ctx_->bss()->beacon_period * kAutoDeauthBcnCountTimeout);
}

bool Station::IsQosReady() const {
  // TODO(NET-567,NET-599): Determine for each outbound data frame,
  // given the result of the dynamic capability negotiation, data frame
  // classification, and QoS policy.

  // Aruba / Ubiquiti are confirmed to be compatible with QoS field for the
  // BlockAck session, independently of 40MHz operation.
  return assoc_ctx_.phy == WLAN_INFO_PHY_TYPE_HT || assoc_ctx_.phy == WLAN_INFO_PHY_TYPE_VHT;
}

uint8_t Station::GetTid() {
  // IEEE Std 802.11-2016, 3.1(Traffic Identifier), 5.1.1.1 (Data Service -
  // General), 9.4.2.30 (Access Policy), 9.2.4.5.2 (TID subfield) Related
  // topics: QoS facility, TSPEC, WM, QMF, TXOP. A TID is from [0, 15], and is
  // assigned to an MSDU in the layers above the MAC. [0, 7] identify Traffic
  // Categories (TCs) [8, 15] identify parameterized Traffic Streams (TSs).

  // TODO(NET-599): Implement QoS policy engine.
  return 0;
}

uint8_t Station::GetTid(const EthFrame& frame) { return GetTid(); }

zx_status_t Station::SetAssocContext(const MgmtFrameView<AssociationResponse>& frame) {
  ZX_DEBUG_ASSERT(join_ctx_ != nullptr);
  assoc_ctx_ = AssocContext{};
  assoc_ctx_.ts_start = timer_mgr_->Now();
  assoc_ctx_.bssid = join_ctx_->bssid();
  assoc_ctx_.aid = frame.body()->aid & kAidMask;
  assoc_ctx_.listen_interval = join_ctx_->listen_interval();

  auto assoc_resp_frame = frame.NextFrame();
  fbl::Span<const uint8_t> ie_chain = assoc_resp_frame.body_data();
  auto bss_assoc_ctx = ParseAssocRespIe(ie_chain);
  if (!bss_assoc_ctx.has_value()) {
    debugf("failed to parse AssocResp\n");
    return ZX_ERR_INVALID_ARGS;
  }
  auto ap = bss_assoc_ctx.value();
  debugjoin("rxed AssocResp:[%s]\n", debug::Describe(ap).c_str());

  ap.cap = frame.body()->cap;

  auto ifc_info = device_->GetWlanInfo().ifc_info;
  auto client = MakeClientAssocCtx(ifc_info, join_ctx_->channel());
  debugjoin("from WlanInfo: [%s]\n", debug::Describe(client).c_str());

  assoc_ctx_.cap = IntersectCapInfo(ap.cap, client.cap);
  assoc_ctx_.rates = IntersectRatesAp(ap.rates, client.rates);

  if (ap.ht_cap.has_value() && client.ht_cap.has_value()) {
    // TODO(porce): Supported MCS Set field from the outcome of the intersection
    // requires the conditional treatment depending on the value of the
    // following fields:
    // - "Tx MCS Set Defined"
    // - "Tx Rx MCS Set Not Equal"
    // - "Tx Maximum Number Spatial Streams Supported"
    // - "Tx Unequal Modulation Supported"
    assoc_ctx_.ht_cap =
        std::make_optional(IntersectHtCap(ap.ht_cap.value(), client.ht_cap.value()));

    // Override the outcome of IntersectHtCap(), which is role agnostic.

    // If AP can't rx STBC, then the client shall not tx STBC.
    // Otherwise, the client shall do what it can do.
    if (ap.ht_cap->ht_cap_info.rx_stbc() == 0) {
      assoc_ctx_.ht_cap->ht_cap_info.set_tx_stbc(0);
    } else {
      assoc_ctx_.ht_cap->ht_cap_info.set_tx_stbc(client.ht_cap->ht_cap_info.tx_stbc());
    }

    // If AP can't tx STBC, then the client shall not expect to rx STBC.
    // Otherwise, the client shall do what it can do.
    if (ap.ht_cap->ht_cap_info.tx_stbc() == 0) {
      assoc_ctx_.ht_cap->ht_cap_info.set_rx_stbc(0);
    } else {
      assoc_ctx_.ht_cap->ht_cap_info.set_rx_stbc(client.ht_cap->ht_cap_info.rx_stbc());
    }

    assoc_ctx_.ht_op = ap.ht_op;
  }
  if (ap.vht_cap.has_value() && client.vht_cap.has_value()) {
    assoc_ctx_.vht_cap =
        std::make_optional(IntersectVhtCap(ap.vht_cap.value(), client.vht_cap.value()));
    assoc_ctx_.vht_op = ap.vht_op;
  }

  assoc_ctx_.phy = join_ctx_->phy();
  if (assoc_ctx_.ht_cap.has_value() && assoc_ctx_.ht_op.has_value()) {
    assoc_ctx_.phy = WLAN_INFO_PHY_TYPE_HT;
  }
  if (assoc_ctx_.vht_cap.has_value() && assoc_ctx_.vht_op.has_value()) {
    assoc_ctx_.phy = WLAN_INFO_PHY_TYPE_VHT;
  }

  // Validate if the AP accepted the requested PHY
  if (assoc_ctx_.phy != join_ctx_->phy()) {
    warnf("PHY for join (%u) and for association (%u) differ. AssocResp:[%s]", join_ctx_->phy(),
          assoc_ctx_.phy, debug::Describe(ap).c_str());
  }

  assoc_ctx_.chan = join_ctx_->channel();
  assoc_ctx_.is_cbw40_rx =
      assoc_ctx_.ht_cap &&
      ap.ht_cap->ht_cap_info.chan_width_set() == HtCapabilityInfo::TWENTY_FORTY &&
      client.ht_cap->ht_cap_info.chan_width_set() == HtCapabilityInfo::TWENTY_FORTY;

  // TODO(porce): Test capabilities and configurations of the client and its
  // BSS.
  // TODO(porce): Ralink dependency on BlockAck, AMPDU handling
  assoc_ctx_.is_cbw40_tx = false;

  debugjoin("final AssocCtx:[%s]\n", debug::Describe(assoc_ctx_).c_str());

  return ZX_OK;
}

zx_status_t Station::NotifyAssocContext() {
  wlan_assoc_ctx_t ddk{};
  assoc_ctx_.bssid.CopyTo(ddk.bssid);
  ddk.aid = assoc_ctx_.aid;
  ddk.listen_interval = assoc_ctx_.listen_interval;
  ddk.phy = assoc_ctx_.phy;
  ddk.chan = assoc_ctx_.chan;

  auto& rates = assoc_ctx_.rates;
  ZX_DEBUG_ASSERT(rates.size() <= WLAN_MAC_MAX_RATES);
  ddk.rates_cnt = static_cast<uint8_t>(rates.size());
  std::copy(rates.cbegin(), rates.cend(), ddk.rates);

  ddk.has_ht_cap = assoc_ctx_.ht_cap.has_value();
  if (assoc_ctx_.ht_cap.has_value()) {
    ddk.ht_cap = assoc_ctx_.ht_cap->ToDdk();
  }

  ddk.has_ht_op = assoc_ctx_.ht_op.has_value();
  if (assoc_ctx_.ht_op.has_value()) {
    ddk.ht_op = assoc_ctx_.ht_op->ToDdk();
  }

  ddk.has_vht_cap = assoc_ctx_.vht_cap.has_value();
  if (assoc_ctx_.vht_cap.has_value()) {
    ddk.vht_cap = assoc_ctx_.vht_cap->ToDdk();
  }

  ddk.has_vht_op = assoc_ctx_.vht_op.has_value();
  if (assoc_ctx_.vht_op.has_value()) {
    ddk.vht_op = assoc_ctx_.vht_op->ToDdk();
  }

  return device_->ConfigureAssoc(&ddk);
}

wlan_stats::ClientMlmeStats Station::stats() const { return stats_.ToFidl(); }

void Station::ResetStats() { stats_.Reset(); }

// TODO(porce): replace SetAssocContext()
std::optional<AssocContext> Station::BuildAssocCtx(const MgmtFrameView<AssociationResponse>& frame,
                                                   const wlan_channel_t& join_chan,
                                                   wlan_info_phy_type_t join_phy,
                                                   uint16_t listen_interval) {
  auto assoc_resp_frame = frame.NextFrame();
  fbl::Span<const uint8_t> ie_chain = assoc_resp_frame.body_data();
  auto bssid = frame.hdr()->addr3;
  auto bss = MakeBssAssocCtx(*frame.body(), ie_chain, bssid);
  if (!bss.has_value()) {
    return {};
  }

  auto client = MakeClientAssocCtx(device_->GetWlanInfo().ifc_info, join_chan);
  auto ctx = IntersectAssocCtx(bss.value(), client);

  // Add info that can't be derived by the intersection
  ctx.ts_start = timer_mgr_->Now();
  ctx.bssid = bss->bssid;
  ctx.aid = bss->aid;
  ctx.phy = ctx.DerivePhy();
  ctx.chan = join_chan;
  ctx.listen_interval = listen_interval;

  if (join_phy != ctx.phy) {
    // This situation is out-of specification, and may happen
    // when what the AP allowed in the Association Response
    // differs from what the AP announced in its beacon.
    // Use the outcome of the association negotiation as the AssocContext's phy.
    // TODO(porce): How should this affect the radio's channel setting?
    warnf("PHY for join (%u) and for association (%u) differ. AssocResp:[%s]", join_phy,
          ctx.DerivePhy(), debug::Describe(bss.value()).c_str());
  }

  return ctx;
}

}  // namespace wlan
