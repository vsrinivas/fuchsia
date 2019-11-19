// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <memory>

#include <wlan/common/buffer_writer.h>
#include <wlan/common/channel.h>
#include <wlan/mlme/ap/infra_bss.h>
#include <wlan/mlme/debug.h>
#include <wlan/mlme/device_caps.h>
#include <wlan/mlme/key.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/mlme.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

#define BSS(b) static_cast<InfraBss*>(b)
InfraBss::InfraBss(DeviceInterface* device, std::unique_ptr<BeaconSender> bcn_sender,
                   const common::MacAddr& bssid, std::unique_ptr<Timer> timer)
    : bssid_(bssid),
      device_(device),
      rust_ap_(nullptr, ap_sta_delete),
      bcn_sender_(std::move(bcn_sender)),
      started_at_(0),
      seq_mgr_(NewSequenceManager()),
      timer_mgr_(std::move(timer)) {
  ZX_DEBUG_ASSERT(bcn_sender_ != nullptr);
  auto rust_device = mlme_device_ops_t{
      .device = static_cast<void*>(this),
      .deliver_eth_frame = [](void* bss, const uint8_t* data, size_t len) -> zx_status_t {
        return BSS(bss)->device_->DeliverEthernet({data, len});
      },
      .send_wlan_frame = [](void* bss, mlme_out_buf_t buf, uint32_t flags) -> zx_status_t {
        auto pkt = FromRustOutBuf(buf);
        if (MgmtFrameView<>::CheckType(pkt.get())) {
          return BSS(bss)->SendMgmtFrame(MgmtFrame<>(std::move(pkt)));
        }
        if (DataFrameView<>::CheckType(pkt.get())) {
          return BSS(bss)->SendDataFrame(DataFrame<>(std::move(pkt)), flags);
        }
        return BSS(bss)->device_->SendWlan(std::move(pkt));
      },
      .get_sme_channel = [](void* bss) -> zx_handle_t {
        return BSS(bss)->device_->GetSmeChannelRef();
      },
      .set_wlan_channel = [](void* bss, wlan_channel_t chan) -> zx_status_t {
        return BSS(bss)->device_->SetChannel(chan);
      },
      .get_wlan_channel = [](void* bss) -> wlan_channel_t {
        return BSS(bss)->device_->GetState()->channel();
      },
      .set_key = [](void* bss, wlan_key_config_t* key) -> zx_status_t {
        return BSS(bss)->device_->SetKey(key);
      },
      .configure_bss = [](void* bss, wlan_bss_config_t* cfg) -> zx_status_t {
        return BSS(bss)->device_->ConfigureBss(cfg);
      },
      .enable_beaconing = [](void* bss, const uint8_t* beacon_tmpl_data, size_t beacon_tmpl_len,
                             size_t tim_ele_offset, uint16_t beacon_interval) -> zx_status_t {
        wlan_bcn_config_t bcn_cfg = {
            .tmpl =
                {
                    .packet_head =
                        {
                            .data_buffer = beacon_tmpl_data,
                            .data_size = beacon_tmpl_len,
                        },
                },
            .tim_ele_offset = tim_ele_offset,
            .beacon_interval = beacon_interval,
        };
        return BSS(bss)->device_->EnableBeaconing(&bcn_cfg);
      },
      .disable_beaconing = [](void* bss) -> zx_status_t {
        return BSS(bss)->device_->EnableBeaconing(nullptr);
      },
  };
  wlan_scheduler_ops_t scheduler = {
      .cookie = this,
      .now = [](void* cookie) -> zx_time_t { return BSS(cookie)->timer_mgr_.Now().get(); },
      .schedule = [](void* cookie, int64_t deadline) -> wlan_scheduler_event_id_t {
        TimeoutId id = {};
        BSS(cookie)->timer_mgr_.Schedule(zx::time(deadline), RustEvent{}, &id);
        return {._0 = id.raw()};
      },
      .cancel = [](void* cookie, wlan_scheduler_event_id_t id) {
        BSS(cookie)->timer_mgr_.Cancel(TimeoutId(id._0));
      },
  };
  rust_ap_ = NewApStation(rust_device, rust_buffer_provider, scheduler, bssid_);
}

InfraBss::~InfraBss() {
  // The BSS should always be explicitly stopped.
  // Throw in debug builds, stop in release ones.
  ZX_DEBUG_ASSERT(!IsStarted());

  // Ensure BSS is stopped correctly.
  Stop();
}

void InfraBss::Start(const MlmeMsg<wlan_mlme::StartRequest>& req) {
  if (IsStarted()) {
    return;
  }

  // Move to requested channel.
  auto chan = wlan_channel_t{
      .primary = req.body()->channel,
      // TODO(WLAN-908): Augment MLME-START.request and forgo a guessing in
      // MLME.
      .cbw = WLAN_CHANNEL_BANDWIDTH__20,
  };

  auto status = device_->SetChannel(chan);
  if (status != ZX_OK) {
    errorf("[infra-bss] [%s] requested start on channel %u failed: %d\n", bssid_.ToString().c_str(),
           req.body()->channel, status);
  }
  chan_ = chan;

  ZX_DEBUG_ASSERT(req.body()->dtim_period > 0);
  if (req.body()->dtim_period == 0) {
    ps_cfg_.SetDtimPeriod(1);
    warnf(
        "[infra-bss] [%s] received start request with reserved DTIM period of "
        "0; falling back "
        "to DTIM period of 1\n",
        bssid_.ToString().c_str());
  } else {
    ps_cfg_.SetDtimPeriod(req.body()->dtim_period);
  }

  debugbss("[infra-bss] [%s] starting BSS\n", bssid_.ToString().c_str());
  debugbss("    SSID: \"%s\"\n", debug::ToAsciiOrHexStr(req.body()->ssid).c_str());
  debugbss("    Beacon Period: %u\n", req.body()->beacon_period);
  debugbss("    DTIM Period: %u\n", req.body()->dtim_period);
  debugbss("    Channel: %u\n", req.body()->channel);

  // Keep track of start request which holds important configuration
  // information.
  req.body()->Clone(&start_req_);

  // Start sending Beacon frames.
  started_at_ = zx_clock_get_monotonic();
  bcn_sender_->Start(this, ps_cfg_, req);

  device_->SetStatus(ETHERNET_STATUS_ONLINE);
}

void InfraBss::Stop() {
  if (!IsStarted()) {
    return;
  }

  debugbss("[infra-bss] [%s] stopping BSS\n", bssid_.ToString().c_str());

  clients_.clear();
  bcn_sender_->Stop();
  started_at_ = 0;
  device_->SetStatus(0);
}

bool InfraBss::IsStarted() { return started_at_ != 0; }

void InfraBss::HandleAnyFrame(std::unique_ptr<Packet> pkt) {
  switch (pkt->peer()) {
    case Packet::Peer::kEthernet: {
      if (auto eth_frame = EthFrameView::CheckType(pkt.get()).CheckLength()) {
        HandleEthFrame(eth_frame.IntoOwned(std::move(pkt)));
      }
      break;
    }
    case Packet::Peer::kWlan:
      HandleAnyWlanFrame(std::move(pkt));
      break;
    default:
      errorf("unknown Packet peer: %u\n", pkt->peer());
      break;
  }
}

void InfraBss::HandleAnyWlanFrame(std::unique_ptr<Packet> pkt) {
  if (auto possible_mgmt_frame = MgmtFrameView<>::CheckType(pkt.get())) {
    if (auto mgmt_frame = possible_mgmt_frame.CheckLength()) {
      HandleAnyMgmtFrame(mgmt_frame.IntoOwned(std::move(pkt)));
    }
  } else if (auto possible_data_frame = DataFrameView<>::CheckType(pkt.get())) {
    if (auto data_frame = possible_data_frame.CheckLength()) {
      HandleAnyDataFrame(data_frame.IntoOwned(std::move(pkt)));
    }
  } else if (auto possible_ctrl_frame = CtrlFrameView<>::CheckType(pkt.get())) {
    if (auto ctrl_frame = possible_ctrl_frame.CheckLength()) {
      HandleAnyCtrlFrame(ctrl_frame.IntoOwned(std::move(pkt)));
    }
  }
}

void InfraBss::HandleAnyMgmtFrame(MgmtFrame<>&& frame) {
  auto mgmt_frame = frame.View();
  bool to_bss = (bssid_ == mgmt_frame.hdr()->addr1 && bssid_ == mgmt_frame.hdr()->addr3);

  // Special treatment for ProbeRequests which can be addressed towards
  // broadcast address.
  if (auto possible_probe_req_frame = mgmt_frame.CheckBodyType<ProbeRequest>()) {
    if (auto mgmt_probe_req_frame = possible_probe_req_frame.CheckLength()) {
      // Drop all ProbeRequests which are neither targeted to this BSS nor to
      // broadcast address.
      auto hdr = mgmt_probe_req_frame.hdr();
      bool to_bcast = hdr->addr1.IsBcast() && hdr->addr3.IsBcast();
      if (!to_bss && !to_bcast) {
        return;
      }

      // Valid ProbeRequest, let BeaconSender process and respond to it.
      auto ra = mgmt_probe_req_frame.hdr()->addr2;
      auto probe_req_frame = mgmt_probe_req_frame.NextFrame();
      fbl::Span<const uint8_t> ie_chain = probe_req_frame.body_data();
      bcn_sender_->SendProbeResponse(ra, ie_chain);
      return;
    }
    return;
  }

  // Drop management frames which are not targeted towards this BSS.
  if (!to_bss) {
    return;
  }

  // Register the client if it's not yet known.
  const auto& client_addr = mgmt_frame.hdr()->addr2;
  if (!HasClient(client_addr)) {
    if (auto auth_frame = mgmt_frame.CheckBodyType<Authentication>().CheckLength()) {
      HandleNewClientAuthAttempt(auth_frame);
    }
  }

  // Forward all frames to the correct client.
  auto client = GetClient(client_addr);
  if (client != nullptr) {
    client->HandleAnyMgmtFrame(std::move(frame));
  }
}

void InfraBss::HandleAnyDataFrame(DataFrame<>&& frame) {
  if (bssid_ != frame.hdr()->addr1) {
    return;
  }

  // Let the correct RemoteClient instance process the received frame.
  const auto& client_addr = frame.hdr()->addr2;
  auto client = GetClient(client_addr);
  if (client != nullptr) {
    client->HandleAnyDataFrame(std::move(frame));
  }
}

void InfraBss::HandleAnyCtrlFrame(CtrlFrame<>&& frame) {
  auto ctrl_frame = frame.View();

  if (auto pspoll_frame = ctrl_frame.CheckBodyType<PsPollFrame>().CheckLength()) {
    if (pspoll_frame.body()->bssid != bssid_) {
      return;
    }

    const auto& client_addr = pspoll_frame.body()->ta;
    auto client = GetClient(client_addr);
    if (client == nullptr) {
      return;
    }

    client->HandleAnyCtrlFrame(std::move(frame));
  }
}

zx_status_t InfraBss::ScheduleTimeout(wlan_tu_t tus, const common::MacAddr& client_addr,
                                      TimeoutId* id) {
  return timer_mgr_.Schedule(timer_mgr_.Now() + WLAN_TU(tus), client_addr, id);
}

void InfraBss::CancelTimeout(TimeoutId id) { timer_mgr_.Cancel(id); }

zx_status_t InfraBss::HandleTimeout() {
  zx_status_t status = timer_mgr_.HandleTimeout([&](auto _now, auto event, auto timeout_id) {
    std::visit([&](auto const& event) {
      using Event = std::decay_t<decltype(event)>;
      if constexpr (std::is_same_v<Event, common::MacAddr>) {
        if (auto client = GetClient(event)) {
          client->HandleTimeout(timeout_id);
        }
      } else if constexpr (std::is_same_v<Event, RustEvent>) {
        ap_sta_timeout_fired(rust_ap_.get(), wlan_scheduler_event_id_t{._0 = timeout_id.raw()});
      } else {
        // Note static_assert(false, ...) doesn't work here, because its value doesn't depend on
        // the type Event and will therefore always trigger. Using is_same_v<Event, Event> forces
        // the dependency on the type, and will only be evaluated if there are cases of Event
        // that are not covered here.
        static_assert(!std::is_same_v<Event, Event>, "cases are not exhaustive!");
      }
    }, event);
  });
  if (status != ZX_OK) {
    errorf("[infra-bss] failed to rearm the timer: %s\n", zx_status_get_string(status));
  }
  return status;
}

void InfraBss::HandleEthFrame(EthFrame&& eth_frame) {
  // Lookup client associated with incoming unicast frame.
  auto& dest_addr = eth_frame.hdr()->dest;
  if (dest_addr.IsUcast()) {
    auto client = GetClient(dest_addr);
    if (client != nullptr) {
      client->HandleAnyEthFrame(std::move(eth_frame));
    }
  } else {
    // Process multicast frames ourselves.
    if (auto data_frame = EthToDataFrame(eth_frame, false)) {
      SendDataFrame(DataFrame<>(data_frame->Take()));
    } else {
      errorf("[infra-bss] [%s] couldn't convert ethernet frame\n", bssid_.ToString().c_str());
    }
  }
}

zx_status_t InfraBss::HandleMlmeMsg(const BaseMlmeMsg& msg) {
  if (auto set_keys_req = msg.As<wlan_mlme::SetKeysRequest>()) {
    return HandleMlmeSetKeysReq(*set_keys_req);
  }

  auto peer_addr = service::GetPeerAddr(msg);
  if (!peer_addr.has_value()) {
    warnf("[infra-bss] received unsupported MLME msg; ordinal: %lu\n", msg.ordinal());
    return ZX_ERR_INVALID_ARGS;
  }

  if (auto client = GetClient(peer_addr.value())) {
    return client->HandleMlmeMsg(msg);
  } else {
    warnf("[infra-bss] unrecognized peer address in MlmeMsg: %s -- ordinal: %lu\n",
          peer_addr.value().ToString().c_str(), msg.ordinal());
  }

  return ZX_OK;
}

void InfraBss::HandleNewClientAuthAttempt(const MgmtFrameView<Authentication>& frame) {
  auto& client_addr = frame.hdr()->addr2;
  ZX_DEBUG_ASSERT(!HasClient(client_addr));

  debugbss("[infra-bss] [%s] new client: %s\n", bssid_.ToString().c_str(),
           client_addr.ToString().c_str());

  // Else, create a new remote client instance.
  auto client = std::make_unique<RemoteClient>(device_,
                                               this,  // bss
                                               this,  // client listener
                                               client_addr);
  clients_.emplace(client_addr, std::move(client));
}

zx_status_t InfraBss::HandleMlmeSetKeysReq(const MlmeMsg<wlan_mlme::SetKeysRequest>& req) {
  debugfn();

  if (!IsRsn()) {
    warnf("[infra-bss] ignoring SetKeysRequest since AP is unprotected\n");
    return ZX_ERR_BAD_STATE;
  }

  for (auto& key_desc : req.body()->keylist) {
    auto key_config = ToKeyConfig(key_desc);
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

void InfraBss::HandleClientFailedAuth(const common::MacAddr& client_addr) {
  debugfn();
  StopTrackingClient(client_addr);
}

void InfraBss::HandleClientDeauth(const common::MacAddr& client_addr) {
  debugfn();
  StopTrackingClient(client_addr);
}

void InfraBss::StopTrackingClient(const wlan::common::MacAddr& client_addr) {
  auto iter = clients_.find(client_addr);
  ZX_DEBUG_ASSERT(iter != clients_.end());
  if (iter == clients_.end()) {
    errorf("[infra-bss] [%s] unknown client deauthenticated: %s\n", bssid_.ToString().c_str(),
           client_addr.ToString().c_str());
    return;
  }

  debugbss("[infra-bss] [%s] removing client %s\n", bssid_.ToString().c_str(),
           client_addr.ToString().c_str());
  clients_.erase(iter);
}

void InfraBss::HandleClientDisassociation(aid_t aid) {
  debugfn();
  ps_cfg_.GetTim()->SetTrafficIndication(aid, false);
}

void InfraBss::HandleClientBuChange(const common::MacAddr& client_addr, aid_t aid,
                                    size_t bu_count) {
  debugfn();
  auto client = GetClient(client_addr);
  ZX_DEBUG_ASSERT(client != nullptr);
  if (client == nullptr) {
    errorf(
        "[infra-bss] [%s] received traffic indication for untracked client: "
        "%s\n",
        bssid_.ToString().c_str(), client_addr.ToString().c_str());
    return;
  }
  ZX_DEBUG_ASSERT(aid != kUnknownAid);
  if (aid == kUnknownAid) {
    errorf(
        "[infra-bss] [%s] received traffic indication from client with unknown "
        "AID: %s\n",
        bssid_.ToString().c_str(), client_addr.ToString().c_str());
    return;
  }

  ps_cfg_.GetTim()->SetTrafficIndication(aid, bu_count > 0);
}

bool InfraBss::HasClient(const common::MacAddr& client) {
  return clients_.find(client) != clients_.end();
}

RemoteClientInterface* InfraBss::GetClient(const common::MacAddr& addr) {
  auto iter = clients_.find(addr);
  if (iter == clients_.end()) {
    return nullptr;
  }
  return iter->second.get();
}

bool InfraBss::ShouldBufferFrame(const common::MacAddr& receiver_addr) const {
  // Buffer non-GCR-SP frames when at least one client is dozing.
  // Note: Currently group addressed service transmission is not supported and
  // thus, every group message should get buffered.
  return receiver_addr.IsGroupAddr() && ps_cfg_.GetTim()->HasDozingClients();
}

zx_status_t InfraBss::BufferFrame(std::unique_ptr<Packet> packet) {
  // Drop oldest frame if queue reached its limit.
  if (bu_queue_.size() >= kMaxGroupAddressedBu) {
    bu_queue_.pop();
    warnf("[infra-bss] [%s] dropping oldest group addressed frame\n", bssid_.ToString().c_str());
  }

  debugps("[infra-bss] [%s] buffer outbound frame\n", bssid_.ToString().c_str());
  bu_queue_.push(std::move(packet));
  ps_cfg_.GetTim()->SetTrafficIndication(kGroupAdressedAid, true);
  return ZX_OK;
}

zx_status_t InfraBss::SendDataFrame(DataFrame<>&& data_frame, uint32_t flags) {
  if (ShouldBufferFrame(data_frame.hdr()->addr1)) {
    return BufferFrame(data_frame.Take());
  }
  return device_->SendWlan(data_frame.Take(), flags);
}

zx_status_t InfraBss::SendMgmtFrame(MgmtFrame<>&& mgmt_frame) {
  if (ShouldBufferFrame(mgmt_frame.hdr()->addr1)) {
    return BufferFrame(mgmt_frame.Take());
  }

  return device_->SendWlan(mgmt_frame.Take());
}

zx_status_t InfraBss::SendOpenAuthFrame(const common::MacAddr& addr, wlan_status_code result) {
  return ap_sta_send_open_auth_frame(rust_ap_.get(), &addr.byte, result);
}

zx_status_t InfraBss::DeliverEthernet(fbl::Span<const uint8_t> frame) {
  return device_->DeliverEthernet(frame);
}

zx_status_t InfraBss::SendNextBu() {
  ZX_DEBUG_ASSERT(bu_queue_.size() > 0);
  if (bu_queue_.empty()) {
    return ZX_ERR_BAD_STATE;
  }

  auto packet = std::move(bu_queue_.front());
  bu_queue_.pop();

  if (auto fc = packet->mut_field<FrameControl>(0)) {
    // Set `more` bit if there are more BU available.
    // IEEE Std 802.11-2016, 9.2.4.1.8
    fc->set_more_data(bu_queue_.size() > 0);
    debugps("[infra-bss] [%s] sent group addressed BU\n", bssid_.ToString().c_str());
    return device_->SendWlan(std::move(packet));
  } else {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
}

std::optional<DataFrame<LlcHeader>> InfraBss::EthToDataFrame(const EthFrame& eth_frame,
                                                             bool needs_protection) {
  size_t payload_len = eth_frame.body_len();
  size_t max_frame_len = DataFrameHeader::max_len() + LlcHeader::max_len() + payload_len;
  auto packet = GetWlanPacket(max_frame_len);
  if (packet == nullptr) {
    errorf(
        "[infra-bss] [%s] cannot convert ethernet to data frame: out of "
        "packets (%zu)\n",
        bssid_.ToString().c_str(), max_frame_len);
    return {};
  }

  BufferWriter w(*packet);
  auto data_hdr = w.Write<DataFrameHeader>();
  data_hdr->fc.set_type(FrameType::kData);
  data_hdr->fc.set_subtype(DataSubtype::kDataSubtype);
  data_hdr->fc.set_from_ds(1);
  data_hdr->fc.set_protected_frame(needs_protection ? 1 : 0);
  data_hdr->addr1 = eth_frame.hdr()->dest;
  data_hdr->addr2 = bssid_;
  data_hdr->addr3 = eth_frame.hdr()->src;
  data_hdr->sc.set_seq(NextSns1(data_hdr->addr1));

  auto llc_hdr = w.Write<LlcHeader>();
  llc_hdr->dsap = kLlcSnapExtension;
  llc_hdr->ssap = kLlcSnapExtension;
  llc_hdr->control = kLlcUnnumberedInformation;
  std::memcpy(llc_hdr->oui, kLlcOui, sizeof(llc_hdr->oui));
  llc_hdr->protocol_id_be = eth_frame.hdr()->ether_type_be;
  w.Write(eth_frame.body_data());

  packet->set_len(w.WrittenBytes());

  // Ralink appears to setup BlockAck session AND AMPDU handling
  // TODO(porce): Use a separate sequence number space in that case
  return DataFrame<LlcHeader>(std::move(packet));
}

void InfraBss::OnPreTbtt() {
  bcn_sender_->UpdateBeacon(ps_cfg_);
  ps_cfg_.NextDtimCount();
}

void InfraBss::OnBcnTxComplete() {
  // Only send out multicast frames if the Beacon we just sent was a DTIM.
  if (ps_cfg_.LastDtimCount() != 0) {
    return;
  }
  if (bu_queue_.size() == 0) {
    return;
  }

  debugps("[infra-bss] [%s] sending %zu group addressed BU\n", bssid_.ToString().c_str(),
          bu_queue_.size());
  while (bu_queue_.size() > 0) {
    auto status = SendNextBu();
    if (status != ZX_OK) {
      errorf("[infra-bss] [%s] could not send group addressed BU: %d\n", bssid_.ToString().c_str(),
             status);
      return;
    }
  }

  ps_cfg_.GetTim()->SetTrafficIndication(kGroupAdressedAid, false);
}

const common::MacAddr& InfraBss::bssid() const { return bssid_; }

uint64_t InfraBss::timestamp() {
  zx_time_t now = zx_clock_get_monotonic();
  zx_duration_t uptime_ns = now - started_at_;
  return uptime_ns / 1000;  // as microseconds
}

uint32_t InfraBss::NextSns1(const common::MacAddr& addr) {
  return mlme_sequence_manager_next_sns1(seq_mgr_.get(), &addr.byte);
}

bool InfraBss::IsRsn() const { return start_req_.rsne.has_value(); }

HtConfig InfraBss::Ht() const {
  // TODO(NET-567): Reflect hardware capabilities and association negotiation
  return HtConfig{
      .ready = true,
      .cbw_40_rx_ready = false,
      .cbw_40_tx_ready = false,
  };
}

const fbl::Span<const SupportedRate> InfraBss::Rates() const {
  const auto rates =
      GetRatesByChannel(device_->GetWlanInfo().ifc_info, device_->GetState()->channel().primary);
  static_assert(sizeof(SupportedRate) == sizeof(rates[0]));
  return {reinterpret_cast<const SupportedRate*>(rates.data()), rates.size()};
}

}  // namespace wlan
