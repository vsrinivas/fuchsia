// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/status.h>

#include <memory>

#include <ddk/hw/wlan/wlaninfo.h>
#include <wlan/common/channel.h>
#include <wlan/mlme/beacon.h>
#include <wlan/mlme/debug.h>
#include <wlan/mlme/device_caps.h>
#include <wlan/mlme/mesh/mesh_mlme.h>
#include <wlan/mlme/mesh/parse_mp_action.h>
#include <wlan/mlme/mesh/write_mp_action.h>
#include <wlan/mlme/service.h>

namespace wlan {

static constexpr size_t kMaxMeshMgmtFrameSize = 1024;
static constexpr size_t kMaxReceivedFrameCacheSize = 500;

namespace wlan_mlme = ::fuchsia::wlan::mlme;
namespace wlan_mesh = ::fuchsia::wlan::mesh;

static wlan_channel_t GetChannel(uint8_t requested_channel) {
  return wlan_channel_t{
      .primary = requested_channel,
      .cbw = WLAN_CHANNEL_BANDWIDTH__20,
  };
}

static MeshConfiguration GetMeshConfig() {
  MeshConfiguration mesh_config = {
      .active_path_sel_proto_id = MeshConfiguration::kHwmp,
      .active_path_sel_metric_id = MeshConfiguration::kAirtime,
      .congest_ctrl_method_id = MeshConfiguration::kCongestCtrlInactive,
      .sync_method_id = MeshConfiguration::kNeighborOffsetSync,
      .auth_proto_id = MeshConfiguration::kNoAuth,
  };

  mesh_config.mesh_capability.set_accepting_additional_peerings(1);
  mesh_config.mesh_capability.set_forwarding(1);
  return mesh_config;
}

static zx_status_t BuildMeshBeacon(wlan_channel_t channel, DeviceInterface* device,
                                   const MlmeMsg<wlan_mlme::StartRequest>& req,
                                   MgmtFrame<Beacon>* buffer, size_t* tim_ele_offset) {
  PsCfg ps_cfg;
  uint8_t dummy;
  auto mesh_config = GetMeshConfig();

  BeaconConfig c = {
      .bssid = device->GetState()->address(),
      .bss_type = BssType::kMesh,
      .ssid = &dummy,
      .ssid_len = 0,
      .rsne = nullptr,
      .rsne_len = 0,
      .beacon_period = req.body()->beacon_period,
      .channel = channel,
      .ps_cfg = &ps_cfg,
      .ht =
          {
              .ready = false,
          },
      .mesh_config = &mesh_config,
      .mesh_id = req.body()->mesh_id.data(),
      .mesh_id_len = req.body()->mesh_id.size(),
  };
  auto rates = GetRatesByChannel(device->GetWlanInfo().ifc_info, channel.primary);
  static_assert(sizeof(SupportedRate) == sizeof(rates[0]));
  c.rates = {reinterpret_cast<const SupportedRate*>(rates.data()), rates.size()};
  return BuildBeacon(c, buffer, tim_ele_offset);
}

MeshMlme::MeshState::MeshState(std::unique_ptr<Timer> timer)
    : hwmp(std::move(timer)), deduplicator(kMaxReceivedFrameCacheSize) {}

MeshMlme::MeshMlme(DeviceInterface* device) : device_(device), seq_mgr_(NewSequenceManager()) {}

zx_status_t MeshMlme::Init() { return ZX_OK; }

zx_status_t MeshMlme::HandleEncodedMlmeMsg(fbl::Span<const uint8_t> msg) {
  return ZX_ERR_NOT_SUPPORTED;
}

static wlan_mesh::MeshPathTable ConvertMeshTable(const PathTable::PathTableByTarget& table) {
  wlan_mesh::MeshPathTable fidl_table;
  for (const auto& it : table) {
    wlan_mesh::MeshPath entry;
    common::MacAddr dest(it.first);
    dest.CopyTo(entry.dest_address.data());
    it.second->next_hop.CopyTo(entry.next_hop.data());
    entry.metric = it.second->metric;
    fidl_table.paths.push_back(entry);
  }
  return fidl_table;
}

zx_status_t MeshMlme::HandleMlmeMsg(const BaseMlmeMsg& msg) {
  if (auto start_req = msg.As<wlan_mlme::StartRequest>()) {
    auto code = Start(*start_req);
    return service::SendStartConfirm(device_, code);
  } else if (auto stop_req = msg.As<wlan_mlme::StopRequest>()) {
    auto code = Stop();
    return service::SendStopConfirm(device_, code);
  } else if (auto mp_open = msg.As<wlan_mlme::MeshPeeringOpenAction>()) {
    SendPeeringOpen(*mp_open);
    return ZX_OK;
  } else if (auto mp_confirm = msg.As<wlan_mlme::MeshPeeringConfirmAction>()) {
    SendPeeringConfirm(*mp_confirm);
    return ZX_OK;
  } else if (auto params = msg.As<wlan_mlme::MeshPeeringParams>()) {
    ConfigurePeering(*params);
    return ZX_OK;
  } else if (auto params = msg.As<wlan_mlme::GetMeshPathTableRequest>()) {
    wlan_mesh::MeshPathTable mesh_table;
    mesh_table.paths.resize(0);
    if (state_) {
      const auto& table = state_->path_table.GetMeshPathTable();
      mesh_table = ConvertMeshTable(table);
    }
    return service::SendMeshPathTable(device_, mesh_table, msg.ordinal(), msg.txid());
  } else {
    return ZX_ERR_NOT_SUPPORTED;
  }
}

wlan_mlme::StartResultCodes MeshMlme::Start(const MlmeMsg<wlan_mlme::StartRequest>& req) {
  if (state_) {
    return wlan_mlme::StartResultCodes::BSS_ALREADY_STARTED_OR_JOINED;
  }

  std::unique_ptr<Timer> timer;
  ObjectId timer_id;
  timer_id.set_subtype(to_enum_type(ObjectSubtype::kTimer));
  timer_id.set_target(to_enum_type(ObjectTarget::kHwmp));
  zx_status_t status = device_->GetTimer(ToPortKey(PortKeyType::kMlme, timer_id.val()), &timer);
  if (status != ZX_OK) {
    errorf("[mesh-mlme] Failed to create the HWMP timer: %s\n", zx_status_get_string(status));
    return wlan_mlme::StartResultCodes::INTERNAL_ERROR;
  }

  wlan_channel_t channel = GetChannel(req.body()->channel);
  status = device_->SetChannel(channel);
  if (status != ZX_OK) {
    errorf("[mesh-mlme] failed to set channel to %s: %s\n", common::ChanStr(channel).c_str(),
           zx_status_get_string(status));
    return wlan_mlme::StartResultCodes::INTERNAL_ERROR;
  }

  MgmtFrame<Beacon> buffer;
  wlan_bcn_config_t cfg = {};
  status = BuildMeshBeacon(channel, device_, req, &buffer, &cfg.tim_ele_offset);
  if (status != ZX_OK) {
    errorf("[mesh-mlme] failed to build a beacon template: %s\n", zx_status_get_string(status));
    return wlan_mlme::StartResultCodes::INTERNAL_ERROR;
  }

  auto packet = buffer.Take();
  cfg.tmpl.packet_head.data_size = packet->len();
  cfg.tmpl.packet_head.data_buffer = packet->data();
  cfg.beacon_interval = req.body()->beacon_period;
  status = device_->EnableBeaconing(&cfg);
  if (status != ZX_OK) {
    errorf("[mesh-mlme] failed to enable beaconing: %s\n", zx_status_get_string(status));
    return wlan_mlme::StartResultCodes::INTERNAL_ERROR;
  }

  device_->SetStatus(ETHERNET_STATUS_ONLINE);
  state_.emplace(std::move(timer));
  return wlan_mlme::StartResultCodes::SUCCESS;
}

wlan_mlme::StopResultCodes MeshMlme::Stop() {
  if (!state_) {
    return wlan_mlme::StopResultCodes::BSS_ALREADY_STOPPED;
  };

  // TODO(gbonik): call clear_assoc for all peers once we have a list of peers

  zx_status_t status = device_->EnableBeaconing(nullptr);
  if (status != ZX_OK) {
    errorf("[mesh-mlme] failed to disable beaconing: %s\n", zx_status_get_string(status));
    return wlan_mlme::StopResultCodes::INTERNAL_ERROR;
  }

  device_->SetStatus(0);
  state_.reset();
  return wlan_mlme::StopResultCodes::SUCCESS;
}

void MeshMlme::SendPeeringOpen(const MlmeMsg<wlan_mlme::MeshPeeringOpenAction>& req) {
  auto packet = GetWlanPacket(kMaxMeshMgmtFrameSize);
  if (packet == nullptr) {
    return;
  }

  BufferWriter w(*packet);
  WriteMpOpenActionFrame(&w, CreateMacHeaderWriter(), *req.body());
  SendMgmtFrame(std::move(packet));
}

void MeshMlme::SendPeeringConfirm(const MlmeMsg<wlan_mlme::MeshPeeringConfirmAction>& req) {
  auto packet = GetWlanPacket(kMaxMeshMgmtFrameSize);
  if (packet == nullptr) {
    return;
  }

  BufferWriter w(*packet);
  WriteMpConfirmActionFrame(&w, CreateMacHeaderWriter(), *req.body());
  SendMgmtFrame(std::move(packet));
}

void MeshMlme::ConfigurePeering(const MlmeMsg<wlan_mlme::MeshPeeringParams>& req) {
  wlan_assoc_ctx ctx = {
      .aid = req.body()->local_aid,
      .phy = WLAN_INFO_PHY_TYPE_OFDM,  // TODO(gbonik): get PHY from
                                       // MeshPeeringParams
      .chan = device_->GetState()->channel(),
      .qos = true,  // all mesh nodes are expected to support QoS frames
      .rates_cnt = static_cast<uint16_t>(std::min(req.body()->rates.size(), sizeof(ctx.rates))),
  };
  memcpy(ctx.bssid, req.body()->peer_sta_address.data(), sizeof(ctx.bssid));
  memcpy(ctx.rates, req.body()->rates.data(), ctx.rates_cnt);
  auto status = device_->ConfigureAssoc(&ctx);
  if (status != ZX_OK) {
    errorf("[mesh-mlme] failed to configure association for mesh peer %s: %s",
           MACSTR(common::MacAddr(req.body()->peer_sta_address)), zx_status_get_string(status));
  }
}

void MeshMlme::SendMgmtFrame(std::unique_ptr<Packet> packet) {
  zx_status_t status = device_->SendWlan(std::move(packet));
  if (status != ZX_OK) {
    errorf("[mesh-mlme] failed to send a mgmt frame: %s\n", zx_status_get_string(status));
  }
}

void MeshMlme::SendMgmtFrames(PacketQueue packets) {
  while (!packets.is_empty()) {
    SendMgmtFrame(packets.Dequeue());
  }
}

void MeshMlme::SendDataFrame(std::unique_ptr<Packet> packet) {
  // TODO(gbonik): select appropriate CBW and PHY per peer.
  // For ath10k, this probably doesn't matter since the driver/firmware should
  // pick the appropriate settings automatically based on the configure_assoc
  // data
  zx_status_t status = device_->SendWlan(std::move(packet));
  if (status != ZX_OK) {
    errorf("[mesh-mlme] failed to send a data frame: %s\n", zx_status_get_string(status));
  }
}

zx_status_t MeshMlme::HandleFramePacket(std::unique_ptr<Packet> pkt) {
  switch (pkt->peer()) {
    case Packet::Peer::kEthernet:
      if (auto eth_frame = EthFrameView::CheckType(pkt.get()).CheckLength()) {
        HandleEthTx(EthFrame(eth_frame.IntoOwned(std::move(pkt))));
      }
      break;
    case Packet::Peer::kWlan:
      return HandleAnyWlanFrame(std::move(pkt));
    default:
      errorf("unknown Packet peer: %u\n", pkt->peer());
      break;
  }
  return ZX_OK;
}

static constexpr size_t GetDataFrameBufferSize(size_t eth_payload_len) {
  return DataFrameHeader::max_len() + sizeof(MeshControl) +
         2 * common::kMacAddrLen  // optional address extension
         + LlcHeader::max_len() + eth_payload_len;
}

void MeshMlme::HandleEthTx(EthFrame&& frame) {
  if (!state_) {
    return;
  }

  auto packet = GetWlanPacket(GetDataFrameBufferSize(frame.body_len()));
  if (packet == nullptr) {
    return;
  }
  BufferWriter w(*packet);
  constexpr uint8_t ttl = 32;

  if (frame.hdr()->dest.IsGroupAddr()) {
    CreateMacHeaderWriter().WriteMeshDataHeaderGroupAddressed(&w, frame.hdr()->dest, self_addr());
    auto mesh_ctl = w.Write<MeshControl>();
    mesh_ctl->ttl = ttl;
    mesh_ctl->seq = mesh_seq_++;
    if (frame.hdr()->src != self_addr()) {
      mesh_ctl->flags.set_addr_ext_mode(kAddrExt4);
      w.WriteValue(frame.hdr()->src);
    }
  } else {
    auto proxy_info = state_->path_table.GetProxyInfo(frame.hdr()->dest);
    auto mesh_dest = proxy_info == nullptr ? frame.hdr()->dest : proxy_info->mesh_target;

    auto path = QueryPathTable(mesh_dest);
    if (path == nullptr) {
      // TODO(gbonik): buffer the frame
      TriggerPathDiscovery(mesh_dest);
      return;
    }
    CreateMacHeaderWriter().WriteMeshDataHeaderIndivAddressed(&w, path->next_hop, mesh_dest,
                                                              self_addr());
    auto mesh_ctl = w.Write<MeshControl>();
    mesh_ctl->ttl = ttl;
    mesh_ctl->seq = mesh_seq_++;
    if (frame.hdr()->src != self_addr() || proxy_info != nullptr) {
      mesh_ctl->flags.set_addr_ext_mode(kAddrExt56);
      w.WriteValue(frame.hdr()->dest);
      w.WriteValue(frame.hdr()->src);
    }
  }

  auto llc_hdr = w.Write<LlcHeader>();
  FillEtherLlcHeader(llc_hdr, frame.hdr()->ether_type_be);
  w.Write(frame.body_data());
  packet->set_len(w.WrittenBytes());
  SendDataFrame(std::move(packet));
}

zx_status_t MeshMlme::HandleAnyWlanFrame(std::unique_ptr<Packet> pkt) {
  if (!state_) {
    return ZX_OK;
  }

  if (auto possible_mgmt_frame = MgmtFrameView<>::CheckType(pkt.get())) {
    if (auto mgmt_frame = possible_mgmt_frame.CheckLength()) {
      return HandleAnyMgmtFrame(mgmt_frame.IntoOwned(std::move(pkt)));
    }
  } else if (DataFrameView<>::CheckType(pkt.get())) {
    HandleDataFrame(std::move(pkt));
    return ZX_OK;
  }
  return ZX_OK;
}

zx_status_t MeshMlme::HandleAnyMgmtFrame(MgmtFrame<>&& frame) {
  auto body = BufferReader(frame.View().body_data());

  switch (frame.hdr()->fc.subtype()) {
    case kAction:
      return HandleActionFrame(*frame.hdr(), &body);
    default:
      return ZX_OK;
  }
}

zx_status_t MeshMlme::HandleActionFrame(const MgmtFrameHeader& mgmt, BufferReader* r) {
  auto action_header = r->Read<ActionFrame>();
  if (action_header == nullptr) {
    return ZX_OK;
  }

  switch (action_header->category) {
    case to_enum_type(action::kSelfProtected):
      return HandleSelfProtectedAction(mgmt.addr2, r);
    case to_enum_type(action::kMesh):
      HandleMeshAction(mgmt, r);
      return ZX_OK;
    default:
      return ZX_OK;
  }
}

zx_status_t MeshMlme::HandleSelfProtectedAction(const common::MacAddr& src_addr, BufferReader* r) {
  auto self_prot_header = r->Read<SelfProtectedActionHeader>();
  if (self_prot_header == nullptr) {
    return ZX_OK;
  }

  switch (self_prot_header->self_prot_action) {
    case action::kMeshPeeringOpen:
      return HandleMpmOpenAction(src_addr, r);
    case action::kMeshPeeringConfirm:
      return HandleMpmConfirmAction(src_addr, r);
    default:
      return ZX_OK;
  }
}

void MeshMlme::HandleMeshAction(const MgmtFrameHeader& mgmt, BufferReader* r) {
  ZX_ASSERT(state_);

  auto mesh_action_header = r->Read<MeshActionHeader>();
  if (mesh_action_header == nullptr) {
    return;
  }

  switch (mesh_action_header->mesh_action) {
    case action::kHwmpMeshPathSelection: {
      // TODO(gbonik): pass the actual airtime metric
      auto packets_to_tx =
          HandleHwmpAction(r->ReadRemaining(), mgmt.addr2, self_addr(), 100,
                           CreateMacHeaderWriter(), &state_->hwmp, &state_->path_table);
      SendMgmtFrames(std::move(packets_to_tx));
      break;
    }
    default:
      break;
  }
}

zx_status_t MeshMlme::HandleMpmOpenAction(const common::MacAddr& src_addr, BufferReader* r) {
  wlan_mlme::MeshPeeringOpenAction action;
  if (!ParseMpOpenAction(r, &action)) {
    return ZX_OK;
  }

  src_addr.CopyTo(action.common.peer_sta_address.data());
  return SendServiceMsg(device_, &action,
                        fuchsia::wlan::mlme::internal::kMLME_IncomingMpOpenAction_GenOrdinal);
}

zx_status_t MeshMlme::HandleMpmConfirmAction(const common::MacAddr& src_addr, BufferReader* r) {
  wlan_mlme::MeshPeeringConfirmAction action;
  if (!ParseMpConfirmAction(r, &action)) {
    return ZX_OK;
  }

  src_addr.CopyTo(action.common.peer_sta_address.data());
  return SendServiceMsg(device_, &action,
                        fuchsia::wlan::mlme::internal::kMLME_IncomingMpConfirmAction_GenOrdinal);
}

const MeshPath* MeshMlme::QueryPathTable(const common::MacAddr& mesh_dest) {
  ZX_ASSERT(state_);

  auto path = state_->path_table.GetPath(mesh_dest);
  if (path == nullptr) {
    return nullptr;
  }

  if (path->expiration_time <= state_->hwmp.timer_mgr.Now()) {
    // Still use the expired path but trigger path discovery.
    // Since we don't have buffering implemented, this is a better alternative
    // than throwing out the frame.
    TriggerPathDiscovery(mesh_dest);
  }
  return path;
}

void MeshMlme::TriggerPathDiscovery(const common::MacAddr& target) {
  ZX_ASSERT(state_);

  PacketQueue packets_to_tx;
  zx_status_t status = InitiatePathDiscovery(target, self_addr(), CreateMacHeaderWriter(),
                                             &state_->hwmp, state_->path_table, &packets_to_tx);
  if (status != ZX_OK) {
    errorf("[mesh-mlme] Failed to initiate path discovery: %s\n", zx_status_get_string(status));
    return;
  }

  SendMgmtFrames(std::move(packets_to_tx));
}

// See IEEE Std 802.11-2016, 9.3.5 (Table 9-42)
static const common::MacAddr& GetDestAddr(const common::ParsedMeshDataHeader& header) {
  if (header.addr_ext.size() == 2) {
    // For proxied individually addressed frames, addr5 is the DA
    return header.addr_ext[0];
  }
  if (header.mac_header.addr4 != nullptr) {
    // For unproxied individually addressed frames, addr3 is the DA
    return header.mac_header.fixed->addr3;
  }
  // For group addressed frames, addr1 is the DA
  return header.mac_header.fixed->addr1;
}

// See IEEE Std 802.11-2016, 10.35.6
static const common::MacAddr& GetMeshSrcAddr(const common::ParsedMeshDataHeader& header) {
  if (header.mac_header.addr4 != nullptr) {
    // Unproxied individually addressed frame
    return *header.mac_header.addr4;
  } else {
    // Unproxied group addressed frame
    return header.mac_header.fixed->addr3;
  }
}

// See IEEE Std 802.11-2016, 9.3.5 (Table 9-42)
static const common::MacAddr& GetSrcAddr(const common::ParsedMeshDataHeader& header) {
  switch (header.addr_ext.size()) {
    case 1:
      // Proxied group addressed frame
      return header.addr_ext[0];
    case 2:
      // Proxied individually addressed frame
      return header.addr_ext[1];
    default:
      // Unproxied
      return GetMeshSrcAddr(header);
  }
}

void MeshMlme::HandleDataFrame(std::unique_ptr<Packet> packet) {
  BufferReader r(*packet);

  auto header = common::ParseMeshDataHeader(&r);
  if (!header) {
    return;
  }

  // Drop frames with 5 addresses (only 3, 4 or 6 addresses are allowed)
  if (header->mac_header.addr4 != nullptr && header->addr_ext.size() == 1) {
    return;
  }

  // Drop reflected frames
  if (header->mac_header.fixed->addr2 == self_addr()) {
    return;
  }

  // TODO(gbonik): drop frames from non-peers

  // Drop if duplicate
  if (state_->deduplicator.DeDuplicate(GetMeshSrcAddr(*header), header->mesh_ctrl->seq)) {
    return;
  }

  if (ShouldDeliverData(header->mac_header)) {
    DeliverData(*header, *packet, r.ReadBytes());
  }

  if (auto next_hop = GetNextHopForForwarding(*header)) {
    ForwardData(*header, std::move(packet), *next_hop);
  }
}

bool MeshMlme::ShouldDeliverData(const common::ParsedDataFrameHeader& header) {
  if (header.addr4 != nullptr) {
    // Individually addressed frame: addr3 is the mesh destination
    return header.fixed->addr3 == self_addr();
  } else {
    // Group-addressed frame: check that addr1 is actually a group address
    return header.fixed->addr1.IsGroupAddr();
  }
}

void MeshMlme::DeliverData(const common::ParsedMeshDataHeader& header,
                           fbl::Span<uint8_t> wlan_frame, size_t payload_offset) {
  ZX_ASSERT(payload_offset >= sizeof(EthernetII));
  auto eth_frame = wlan_frame.subspan(payload_offset - sizeof(EthernetII));
  ZX_ASSERT(eth_frame.size() >= sizeof(EthernetII));

  uint8_t old[sizeof(EthernetII)];
  memcpy(old, eth_frame.data(), sizeof(EthernetII));

  // Construct the header in a separate chunk of memory to make sure we don't
  // overwrite the data while reading it at the same time
  EthernetII eth_hdr = {
      .dest = GetDestAddr(header),
      .src = GetSrcAddr(header),
      .ether_type_be = header.llc->protocol_id_be,
  };

  memcpy(eth_frame.data(), &eth_hdr, sizeof(EthernetII));
  zx_status_t status = device_->DeliverEthernet(eth_frame);

  // Restore the original buffer to make sure we don't confuse the caller
  memcpy(eth_frame.data(), &old, sizeof(EthernetII));

  if (status != ZX_OK) {
    errorf("[mesh-mlme] Failed to deliver an ethernet frame: %s\n", zx_status_get_string(status));
  }
}

std::optional<common::MacAddr> MeshMlme::GetNextHopForForwarding(
    const common::ParsedMeshDataHeader& header) {
  ZX_ASSERT(state_);

  if (header.mesh_ctrl->ttl <= 1) {
    return {};
  }

  if (header.mac_header.addr4 != nullptr) {
    // Individually addressed frame: addr3 is the mesh destination
    if (header.mac_header.fixed->addr3 == self_addr()) {
      return {};
    }
    auto path = QueryPathTable(header.mac_header.fixed->addr3);
    if (path == nullptr) {
      // Notify the transmitter about the missing path
      auto packets_to_tx =
          OnMissingForwardingPath(header.mac_header.fixed->addr2, header.mac_header.fixed->addr3,
                                  CreateMacHeaderWriter(), &state_->hwmp);
      SendMgmtFrames(std::move(packets_to_tx));
      return {};
    }
    return {path->next_hop};
  } else {
    // Group-addressed frame: check that addr1 is actually a group address
    if (!header.mac_header.fixed->addr1.IsGroupAddr()) {
      return {};
    }
    return {header.mac_header.fixed->addr1};
  }
}

void MeshMlme::ForwardData(const common::ParsedMeshDataHeader& header,
                           std::unique_ptr<Packet> packet, const common::MacAddr& next_hop) {
  // const_cast is safe because we have a mutable pointer to data in `packet`
  auto mac_header = const_cast<DataFrameHeader*>(header.mac_header.fixed);
  mac_header->addr1 = next_hop;
  mac_header->addr2 = self_addr();

  if (header.mac_header.qos_ctrl) {
    uint32_t seq = mlme_sequence_manager_next_sns2(seq_mgr_.get(), &mac_header->addr1.byte,
                                                   header.mac_header.qos_ctrl->tid());
    mac_header->sc.set_seq(seq);
  } else {
    uint32_t seq = mlme_sequence_manager_next_sns1(seq_mgr_.get(), &mac_header->addr1.byte);
    mac_header->sc.set_seq(seq);
  }

  auto mesh_ctrl = const_cast<MeshControl*>(header.mesh_ctrl);
  mesh_ctrl->ttl -= 1;

  SendDataFrame(std::move(packet));
}

zx_status_t MeshMlme::HandleTimeout(const ObjectId id) {
  if (!state_) {
    return ZX_OK;
  }

  switch (id.target()) {
    case to_enum_type(ObjectTarget::kHwmp): {
      PacketQueue packets_to_tx;
      zx_status_t status = HandleHwmpTimeout(self_addr(), CreateMacHeaderWriter(), &state_->hwmp,
                                             state_->path_table, &packets_to_tx);
      if (status != ZX_OK) {
        errorf("[mesh-mlme] Failed to rearm the HWMP timer: %s\n", zx_status_get_string(status));
        return status;
      }

      SendMgmtFrames(std::move(packets_to_tx));
      break;
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

MacHeaderWriter MeshMlme::CreateMacHeaderWriter() {
  return MacHeaderWriter{self_addr(), seq_mgr_.get()};
}

}  // namespace wlan
