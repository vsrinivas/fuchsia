// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/ap/infra_bss.h>

#include <wlan/mlme/mlme.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>

#include <zircon/syscalls.h>

namespace wlan {

InfraBss::InfraBss(DeviceInterface* device, fbl::unique_ptr<BeaconSender> bcn_sender,
                   const common::MacAddr& bssid)
    : bssid_(bssid), device_(device), bcn_sender_(fbl::move(bcn_sender)) {
    ZX_DEBUG_ASSERT(bcn_sender_ != nullptr);
}

InfraBss::~InfraBss() {
    // The BSS should always be explicitly stopped.
    // Throw in debug builds, stop in release ones.
    ZX_DEBUG_ASSERT(!IsStarted());

    // Ensure BSS is stopped correctly.
    Stop();
}

void InfraBss::Start(const wlan_mlme::StartRequest& req) {
    if (IsStarted()) { return; }

    // Move to requested channel.
    auto chan = wlan_channel_t{
        .primary = req.channel,
        .cbw = CBW20,
    };

    auto status = device_->SetChannel(chan);
    if (status != ZX_OK) {
        errorf("[infra-bss] [%s] requested start on channel %u failed: %d\n",
               bssid_.ToString().c_str(), req.channel, status);
    }
    chan_ = chan;

    debugbss("[infra-bss] [%s] starting BSS\n", bssid_.ToString().c_str());
    debugbss("    SSID: %s\n", req.ssid->data());
    debugbss("    Beacon Period: %u\n", req.beacon_period);
    debugbss("    DTIM Period: %u\n", req.dtim_period);
    debugbss("    Channel: %u\n", req.channel);

    // Start sending Beacon frames.
    started_at_ = zx_clock_get(ZX_CLOCK_MONOTONIC);
    bcn_sender_->Start(this, req);
}

void InfraBss::Stop() {
    if (!IsStarted()) { return; }

    debugbss("[infra-bss] [%s] stopping BSS\n", bssid_.ToString().c_str());

    clients_.Clear();
    bcn_sender_->Stop();
    started_at_ = 0;
}

bool InfraBss::IsStarted() {
    return started_at_ != 0;
}

zx_status_t InfraBss::HandleTimeout(const common::MacAddr& client_addr) {
    ZX_DEBUG_ASSERT(clients_.Has(client_addr));
    if (clients_.Has(client_addr)) { clients_.GetClient(client_addr)->HandleTimeout(); }
    return ZX_OK;
}

zx_status_t InfraBss::HandleMgmtFrame(const MgmtFrameHeader& hdr) {
    bool to_bss = (bssid_ == hdr.addr1 && bssid_ == hdr.addr3);

    // Special treatment for ProbeRequests which can be addressed towards broadcast address.
    if (hdr.fc.subtype() == ManagementSubtype::kProbeRequest) {
        // Drop all ProbeRequests which are neither targeted to this BSS nor to broadcast address.
        bool to_bcast = (common::kBcastMac == hdr.addr1 && common::kBcastMac == hdr.addr3);
        if (!to_bss && !to_bcast) { return ZX_ERR_STOP; }

        // Valid ProbeRequest, forward to BeaconSender for processing.
        ForwardCurrentFrameTo(bcn_sender_.get());
        return ZX_OK;
    }

    // Drop management frames which are not targeted towards this BSS.
    if (!to_bss) { return ZX_ERR_STOP; }

    // Let the correct RemoteClient instance process the received frame.
    auto& client_addr = hdr.addr2;
    if (clients_.Has(client_addr)) { ForwardCurrentFrameTo(clients_.GetClient(client_addr)); }
    return ZX_OK;
}

zx_status_t InfraBss::HandleDataFrame(const DataFrameHeader& hdr) {
    if (bssid_ != hdr.addr1) { return ZX_ERR_STOP; }

    // Let the correct RemoteClient instance process the received frame.
    auto& client_addr = hdr.addr2;
    if (clients_.Has(client_addr)) { ForwardCurrentFrameTo(clients_.GetClient(client_addr)); }
    return ZX_OK;
}

zx_status_t InfraBss::HandleAuthentication(const ImmutableMgmtFrame<Authentication>& frame,
                                           const wlan_rx_info_t& rxinfo) {
    // If the client is already known, there is no work to be done here.
    auto& client_addr = frame.hdr->addr2;
    if (clients_.Has(client_addr)) { return ZX_OK; }

    // Else, create a new remote client instance.
    fbl::unique_ptr<Timer> timer = nullptr;
    auto status = CreateClientTimer(client_addr, &timer);
    if (status != ZX_OK) { return status; }

    auto client = fbl::make_unique<RemoteClient>(device_, fbl::move(timer),
                                                 this,  // bss
                                                 this,  // client listener
                                                 client_addr);
    clients_.Add(client_addr, fbl::move(client));

    // Note: usually, HandleMgmtFrame(...) will forward incoming frames to the corresponding
    // clients. However, Authentication frames will add new clients and hence, this frame must be
    // forwarded manually to the newly added client.
    ForwardCurrentFrameTo(clients_.GetClient(client_addr));
    return ZX_OK;
}

zx_status_t InfraBss::HandlePsPollFrame(const ImmutableCtrlFrame<PsPollFrame>& frame,
                                        const wlan_rx_info_t& rxinfo) {
    auto& client_addr = frame.hdr->ta;
    if (frame.hdr->bssid != bssid_) { return ZX_ERR_STOP; }
    if (clients_.GetClientAid(client_addr) != frame.hdr->aid) { return ZX_ERR_STOP; }

    ForwardCurrentFrameTo(clients_.GetClient(client_addr));
    return ZX_OK;
}

void InfraBss::HandleClientStateChange(const common::MacAddr& client, RemoteClient::StateId from,
                                       RemoteClient::StateId to) {
    debugfn();
    // Ignore when transitioning from `uninitialized` state.
    if (from == RemoteClient::StateId::kUninitialized) { return; }

    ZX_DEBUG_ASSERT(clients_.Has(client));
    if (!clients_.Has(client)) {
        errorf("[infra-bss] [%s] state change (%hhu, %hhu) reported for unknown client: %s\n",
               bssid_.ToString().c_str(), from, to, client.ToString().c_str());
        return;
    }

    // If client enters deauthenticated state after being authenticated, remove client.
    if (to == RemoteClient::StateId::kDeauthenticated) {
        auto status = clients_.Remove(client);
        if (status != ZX_OK) {
            errorf("[infra-bss] [%s] couldn't remove client %s: %d\n", bssid_.ToString().c_str(),
                   client.ToString().c_str(), status);
        }
    }
}

void InfraBss::HandleClientBuChange(const common::MacAddr& client, size_t bu_count) {
    debugfn();
    auto aid = clients_.GetClientAid(client);
    ZX_DEBUG_ASSERT(aid != kUnknownAid);
    if (aid == kUnknownAid) {
        errorf("[infra-bss] [%s] received traffic indication from client with unknown AID: %s\n",
               bssid_.ToString().c_str(), client.ToString().c_str());
        return;
    }

    tim_.SetTrafficIndication(aid, bu_count > 0);
    bcn_sender_->UpdateBeacon(tim_);
}

zx_status_t InfraBss::AssignAid(const common::MacAddr& client, aid_t* out_aid) {
    debugfn();
    auto status = clients_.AssignAid(client, out_aid);
    if (status != ZX_OK) {
        errorf("[infra-bss] [%s] couldn't assign AID to client %s: %d\n", bssid_.ToString().c_str(),
               client.ToString().c_str(), status);
        return status;
    }
    return ZX_OK;
}

zx_status_t InfraBss::ReleaseAid(const common::MacAddr& client) {
    debugfn();
    auto aid = clients_.GetClientAid(client);
    ZX_DEBUG_ASSERT(aid != kUnknownAid);
    if (aid == kUnknownAid) {
        errorf("[infra-bss] [%s] tried releasing AID for unknown client: %s\n",
               bssid_.ToString().c_str(), client.ToString().c_str());
        return ZX_ERR_NOT_FOUND;
    }

    tim_.SetTrafficIndication(aid, false);
    bcn_sender_->UpdateBeacon(tim_);
    return clients_.ReleaseAid(client);
}

fbl::unique_ptr<Buffer> InfraBss::GetPowerSavingBuffer(size_t len) {
    return GetBuffer(len);
}

zx_status_t InfraBss::CreateClientTimer(const common::MacAddr& client_addr,
                                        fbl::unique_ptr<Timer>* out_timer) {
    ObjectId timer_id;
    timer_id.set_subtype(to_enum_type(ObjectSubtype::kTimer));
    timer_id.set_target(to_enum_type(ObjectTarget::kBss));
    timer_id.set_mac(client_addr.ToU64());
    zx_status_t status =
        device_->GetTimer(ToPortKey(PortKeyType::kMlme, timer_id.val()), out_timer);
    if (status != ZX_OK) {
        errorf("[infra-bss] [%s] could not create bss timer: %d\n", bssid_.ToString().c_str(),
               status);
        return status;
    }
    return ZX_OK;
}

const common::MacAddr& InfraBss::bssid() const {
    return bssid_;
}

uint64_t InfraBss::timestamp() {
    zx_time_t now = zx_clock_get(ZX_CLOCK_MONOTONIC);
    zx_duration_t uptime_ns = now - started_at_;
    return uptime_ns / 1000;  // as microseconds
}

seq_t InfraBss::NextSeq(const MgmtFrameHeader& hdr) {
    return NextSeqNo(hdr, &seq_);
}

seq_t InfraBss::NextSeq(const MgmtFrameHeader& hdr, uint8_t aci) {
    return NextSeqNo(hdr, aci, &seq_);
}

seq_t InfraBss::NextSeq(const DataFrameHeader& hdr) {
    return NextSeqNo(hdr, &seq_);
}

bool InfraBss::IsHTReady() const {
    // TODO(NET-567): Reflect hardware capabilities and association negotiation
    return false;
}

bool InfraBss::IsCbw40RxReady() const {
    // TODO(NET-567): Reflect hardware capabilities and association negotiation
    return false;
}

bool InfraBss::IsCbw40TxReady() const {
    // TODO(NET-567): Reflect hardware capabilities and association negotiation
    return false;
}

HtCapabilities InfraBss::BuildHtCapabilities() const {
    // TODO(porce): Find intersection of
    // - BSS capabilities
    // - Client radio capabilities
    // - Client configuration

    // Static cooking for Proof-of-Concept
    HtCapabilities htc;
    HtCapabilityInfo& hci = htc.ht_cap_info;

    hci.set_ldpc_coding_cap(0);  // Ralink RT5370 is incapable of LDPC.

    if (IsCbw40RxReady()) {
        hci.set_chan_width_set(HtCapabilityInfo::TWENTY_FORTY);
    } else {
        hci.set_chan_width_set(HtCapabilityInfo::TWENTY_ONLY);
    }

    hci.set_sm_power_save(HtCapabilityInfo::DISABLED);
    hci.set_greenfield(0);
    hci.set_short_gi_20(1);
    hci.set_short_gi_40(1);
    hci.set_tx_stbc(0);  // No plan to support STBC Tx
    hci.set_rx_stbc(1);  // one stream.
    hci.set_delayed_block_ack(0);
    hci.set_max_amsdu_len(HtCapabilityInfo::OCTETS_7935);  // Aruba
    // hci.set_max_amsdu_len(HtCapabilityInfo::OCTETS_3839);  // TP-Link
    hci.set_dsss_in_40(0);
    hci.set_intolerant_40(0);
    hci.set_lsig_txop_protect(0);

    AmpduParams& ampdu = htc.ampdu_params;
    ampdu.set_exponent(3);                                // 65535 bytes
    ampdu.set_min_start_spacing(AmpduParams::FOUR_USEC);  // Aruba
    // ampdu.set_min_start_spacing(AmpduParams::EIGHT_USEC);  // TP-Link
    // ampdu.set_min_start_spacing(AmpduParams::SIXTEEN_USEC);

    SupportedMcsSet& mcs = htc.mcs_set;
    mcs.rx_mcs_head.set_bitmask(0xff);  // MCS 0-7
    // mcs.rx_mcs_head.set_bitmask(0xffff);  // MCS 0-15

    HtExtCapabilities& hec = htc.ht_ext_cap;
    hec.set_pco(0);
    hec.set_pco_transition(HtExtCapabilities::PCO_RESERVED);
    hec.set_mcs_feedback(HtExtCapabilities::MCS_NOFEEDBACK);
    hec.set_htc_ht_support(0);
    hec.set_rd_responder(0);

    TxBfCapability& txbf = htc.txbf_cap;
    txbf.set_implicit_rx(0);
    txbf.set_rx_stag_sounding(0);
    txbf.set_tx_stag_sounding(0);
    txbf.set_rx_ndp(0);
    txbf.set_tx_ndp(0);
    txbf.set_implicit(0);
    txbf.set_calibration(TxBfCapability::CALIBRATION_NONE);
    txbf.set_csi(0);
    txbf.set_noncomp_steering(0);
    txbf.set_comp_steering(0);
    txbf.set_csi_feedback(TxBfCapability::FEEDBACK_NONE);
    txbf.set_noncomp_feedback(TxBfCapability::FEEDBACK_NONE);
    txbf.set_comp_feedback(TxBfCapability::FEEDBACK_NONE);
    txbf.set_min_grouping(TxBfCapability::MIN_GROUP_ONE);
    txbf.set_csi_antennas_human(1);           // 1 antenna
    txbf.set_noncomp_steering_ants_human(1);  // 1 antenna
    txbf.set_comp_steering_ants_human(1);     // 1 antenna
    txbf.set_csi_rows_human(1);               // 1 antenna
    txbf.set_chan_estimation_human(1);        // # space-time stream

    AselCapability& asel = htc.asel_cap;
    asel.set_asel(0);
    asel.set_csi_feedback_tx_asel(0);
    asel.set_explicit_csi_feedback(0);
    asel.set_antenna_idx_feedback(0);
    asel.set_rx_asel(0);
    asel.set_tx_sounding_ppdu(0);

    return htc;  // 28 bytes.
}

HtOperation InfraBss::BuildHtOperation(const wlan_channel_t& chan) const {
    // TODO(porce): Query the BSS internal data to fill up the parameters.
    HtOperation hto;

    hto.primary_chan = chan.primary;
    HtOpInfoHead& head = hto.head;
    head.set_secondary_chan_offset(HtOpInfoHead::SECONDARY_NONE);  // TODO(porce): CBW
    head.set_sta_chan_width(HtOpInfoHead::TWENTY);                 // TODO(porce): CBW
    head.set_rifs_mode(0);
    head.set_reserved1(0);  // TODO(porce): Tweak this for 802.11n D1.10 compatibility
    head.set_ht_protect(HtOpInfoHead::NONE);
    head.set_nongreenfield_present(1);
    head.set_reserved2(0);  // TODO(porce): Tweak this for 802.11n D1.10 compatibility
    head.set_obss_non_ht(0);
    head.set_center_freq_seg2(0);
    head.set_dual_beacon(0);
    head.set_dual_cts_protect(0);

    HtOpInfoTail& tail = hto.tail;
    tail.set_stbc_beacon(0);
    tail.set_lsig_txop_protect(0);
    tail.set_pco_active(0);
    tail.set_pco_phase(0);

    SupportedMcsSet& mcs = hto.mcs_set;
    mcs.rx_mcs_head.set_bitmask(0xff);  // MCS 0-7

    return hto;
}

}  // namespace wlan
