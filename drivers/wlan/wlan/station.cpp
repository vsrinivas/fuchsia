// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "station.h"

#include "device_interface.h"
#include "logging.h"
#include "mac_frame.h"
#include "packet.h"
#include "serialize.h"
#include "timer.h"

#include <cstring>
#include <utility>

namespace wlan {

static constexpr zx_duration_t kAssocTimeoutTu = 20;
static constexpr zx_duration_t kSignalReportTimeoutTu = 10;

Station::Station(DeviceInterface* device, fbl::unique_ptr<Timer> timer)
    : device_(device), timer_(std::move(timer)) {
    (void)assoc_timeout_;
    bssid_.Reset();
}

void Station::Reset() {
    debugfn();

    timer_->CancelTimer();
    state_ = WlanState::kUnjoined;
    bss_.reset();
    join_timeout_ = 0;
    auth_timeout_ = 0;
    last_seen_ = 0;
    bssid_.Reset();
}

zx_status_t Station::Join(JoinRequestPtr req) {
    debugfn();

    ZX_DEBUG_ASSERT(!req.is_null());

    if (req->selected_bss.is_null()) {
        errorf("bad join request\n");
        // Don't reset because of a bad request. Just send the response.
        return SendJoinResponse();
    }

    if (state_ != WlanState::kUnjoined) {
        warnf("already joined; resetting station\n");
        Reset();
    }

    bss_ = std::move(req->selected_bss);
    bssid_.Set(bss_->bssid.data());
    debugjoin("setting channel to %u\n", bss_->channel);
    zx_status_t status = device_->SetChannel(wlan_channel_t{bss_->channel});
    if (status != ZX_OK) {
        errorf("could not set wlan channel: %d\n", status);
        Reset();
        SendJoinResponse();
        return status;
    }

    join_timeout_ = deadline_after_bcn_period(req->join_failure_timeout);
    status = timer_->SetTimer(join_timeout_);
    if (status != ZX_OK) {
        errorf("could not set join timer: %d\n", status);
        Reset();
        SendJoinResponse();
    }

    // TODO(hahnr): Update when other BSS types are supported.
    device_->SetBss(bssid_, WLAN_BSS_TYPE_INFRASTRUCTURE);
    return status;
}

zx_status_t Station::Authenticate(AuthenticateRequestPtr req) {
    debugfn();

    ZX_DEBUG_ASSERT(!req.is_null());

    if (bss_.is_null()) { return ZX_ERR_BAD_STATE; }

    // TODO(tkilbourn): better result codes
    if (!bss_->bssid.Equals(req->peer_sta_address)) {
        errorf("cannot authenticate before joining\n");
        return SendAuthResponse(AuthenticateResultCodes::REFUSED);
    }
    if (state_ == WlanState::kUnjoined) {
        errorf("must join before authenticating\n");
        return SendAuthResponse(AuthenticateResultCodes::REFUSED);
    }
    if (state_ != WlanState::kUnauthenticated) {
        warnf("already authenticated; sending request anyway\n");
    }
    if (req->auth_type != AuthenticationTypes::OPEN_SYSTEM) {
        // TODO(tkilbourn): support other authentication types
        // TODO(tkilbourn): set the auth_alg_ when we support other authentication types
        errorf("only OpenSystem authentication is supported\n");
        return SendAuthResponse(AuthenticateResultCodes::REFUSED);
    }

    debugjoin("authenticating to %s\n", MACSTR(bssid_));

    // TODO(tkilbourn): better size management
    size_t auth_len = sizeof(MgmtFrameHeader) + sizeof(Authentication);
    fbl::unique_ptr<Buffer> buffer = GetBuffer(auth_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    const MacAddr& mymac = device_->GetState()->address();

    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), auth_len));
    packet->clear();
    packet->set_peer(Packet::Peer::kWlan);
    auto hdr = packet->mut_field<MgmtFrameHeader>(0);
    hdr->fc.set_type(kManagement);
    hdr->fc.set_subtype(kAuthentication);

    hdr->addr1 = bssid_;
    hdr->addr2 = mymac;
    hdr->addr3 = bssid_;

    hdr->sc.set_seq(next_seq());

    auto auth = packet->mut_field<Authentication>(sizeof(MgmtFrameHeader));
    // TODO(tkilbourn): this assumes Open System authentication
    auth->auth_algorithm_number = auth_alg_;
    auth->auth_txn_seq_number = 1;
    auth->status_code = 0;  // Reserved, so set to 0

    zx_status_t status = device_->SendWlan(std::move(packet));
    if (status != ZX_OK) {
        errorf("could not send auth packet: %d\n", status);
        SendAuthResponse(AuthenticateResultCodes::REFUSED);
        return status;
    }

    auth_timeout_ = deadline_after_bcn_period(req->auth_failure_timeout);
    status = timer_->SetTimer(auth_timeout_);
    if (status != ZX_OK) {
        errorf("could not set auth timer: %d\n", status);
        // This is the wrong result code, but we need to define our own codes at some later time.
        SendAuthResponse(AuthenticateResultCodes::AUTH_FAILURE_TIMEOUT);
        // TODO(tkilbourn): reset the station?
    }
    return status;
}

zx_status_t Station::Associate(AssociateRequestPtr req) {
    debugfn();

    ZX_DEBUG_ASSERT(!req.is_null());

    if (bss_.is_null()) { return ZX_ERR_BAD_STATE; }

    // TODO(tkilbourn): better result codes
    if (!bss_->bssid.Equals(req->peer_sta_address)) {
        errorf("bad peer STA address for association\n");
        return SendAuthResponse(AuthenticateResultCodes::REFUSED);
    }
    if (state_ == WlanState::kUnjoined || state_ == WlanState::kUnauthenticated) {
        errorf("must authenticate before associating\n");
        return SendAuthResponse(AuthenticateResultCodes::REFUSED);
    }
    if (state_ == WlanState::kAssociated) {
        warnf("already authenticated; sending request anyway\n");
    }

    debugjoin("associating to %s\n", MACSTR(bssid_));

    // TODO(tkilbourn): better size management; for now reserve 128 bytes for Association elements
    size_t assoc_len = sizeof(MgmtFrameHeader) + sizeof(AssociationRequest) + 128;
    fbl::unique_ptr<Buffer> buffer = GetBuffer(assoc_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    const MacAddr& mymac = device_->GetState()->address();

    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), assoc_len));
    packet->clear();
    packet->set_peer(Packet::Peer::kWlan);
    auto hdr = packet->mut_field<MgmtFrameHeader>(0);
    hdr->fc.set_type(kManagement);
    hdr->fc.set_subtype(kAssociationRequest);

    hdr->addr1 = bssid_;
    hdr->addr2 = mymac;
    hdr->addr3 = bssid_;

    hdr->sc.set_seq(next_seq());

    // TODO(tkilbourn): a lot of this is hardcoded for now. Use device capabilities to set up the
    // request.
    auto assoc = packet->mut_field<AssociationRequest>(sizeof(MgmtFrameHeader));
    assoc->cap.set_ess(1);
    assoc->listen_interval = 0;
    ElementWriter w(assoc->elements,
                    packet->len() - sizeof(MgmtFrameHeader) - sizeof(AssociationRequest));
    if (!w.write<SsidElement>(bss_->ssid.data())) {
        errorf("could not write ssid \"%s\" to association request\n", bss_->ssid.data());
        SendAssocResponse(AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        return ZX_ERR_IO;
    }
    // TODO(tkilbourn): add extended rates support to get the rest of 802.11g rates.
    // TODO(tkilbourn): determine these rates based on hardware and the AP
    std::vector<uint8_t> rates = {0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24};

    if (!w.write<SupportedRatesElement>(std::move(rates))) {
        errorf("could not write supported rates\n");
        SendAssocResponse(AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        return ZX_ERR_IO;
    }

    std::vector<uint8_t> ext_rates = {0x30, 0x48, 0x60, 0x6c};
    if (!w.write<ExtendedSupportedRatesElement>(std::move(ext_rates))) {
        errorf("could not write extended supported rates\n");
        SendAssocResponse(AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        return ZX_ERR_IO;
    }

    // Write RSNE from MLME-Association.request if available.
    if (req->rsn) {
        if (!w.write<RsnElement>(req->rsn.data(), req->rsn.size())) { return ZX_ERR_IO; }
    }

    if (IsHTReady()) {
        HtCapabilities htc = BuildHtCapabilities();
        if (!w.write<HtCapabilities>(htc.ht_cap_info, htc.ampdu_params, htc.mcs_set, htc.ht_ext_cap,
                                     htc.txbf_cap, htc.asel_cap)) {
            errorf("could not write HtCapabilities\n");
            SendAssocResponse(AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
            return ZX_ERR_IO;
        }
    }

    // Validate the request in debug mode
    ZX_DEBUG_ASSERT(assoc->Validate(w.size()));

    size_t actual_len = sizeof(MgmtFrameHeader) + sizeof(AssociationRequest) + w.size();
    zx_status_t status = packet->set_len(actual_len);
    if (status != ZX_OK) {
        errorf("could not set packet length to %zu: %d\n", actual_len, status);
        SendAssocResponse(AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        return status;
    }

    status = device_->SendWlan(std::move(packet));
    if (status != ZX_OK) {
        errorf("could not send assoc packet: %d\n", status);
        SendAssocResponse(AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        return status;
    }

    // TODO(tkilbourn): get the assoc timeout from somewhere
    assoc_timeout_ = deadline_after_bcn_period(kAssocTimeoutTu);
    status = timer_->SetTimer(assoc_timeout_);
    if (status != ZX_OK) {
        errorf("could not set auth timer: %d\n", status);
        // This is the wrong result code, but we need to define our own codes at some later time.
        SendAssocResponse(AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        // TODO(tkilbourn): reset the station?
    }
    return status;
}

// TODO(hahnr): Support ProbeResponses.
zx_status_t Station::HandleBeacon(const Packet* packet) {
    debugfn();

    ZX_DEBUG_ASSERT(!bss_.is_null());

    auto rxinfo = packet->ctrl_data<wlan_rx_info_t>();
    ZX_DEBUG_ASSERT(rxinfo);

    auto hdr = packet->field<MgmtFrameHeader>(0);

    if (hdr->addr3 != MacAddr(bss_->bssid.data())) {
        // Not our beacon -- this shouldn't happen because the Mlme should not have routed this
        // packet to this Station.
        ZX_DEBUG_ASSERT(false);
        return ZX_ERR_BAD_STATE;
    }

    avg_rssi_.add(rxinfo->rssi);

    // TODO(tkilbourn): update any other info (like rolling average of rssi)
    last_seen_ = timer_->Now();
    if (join_timeout_ > 0) {
        join_timeout_ = 0;
        timer_->CancelTimer();
        state_ = WlanState::kUnauthenticated;
        debugjoin("joined %s\n", bss_->ssid.data());
        return SendJoinResponse();
    }

    auto bcn = packet->field<Beacon>(sizeof(MgmtFrameHeader));
    size_t elt_len = packet->len() - sizeof(MgmtFrameHeader) - sizeof(Beacon);
    ElementReader reader(bcn->elements, elt_len);

    while (reader.is_valid()) {
        const ElementHeader* hdr = reader.peek();
        if (hdr == nullptr) break;

        switch (hdr->id) {
        case element_id::kTim: {
            auto tim = reader.read<TimElement>();
            if (tim == nullptr) goto done_iter;
            if (tim->traffic_buffered(aid_)) { SendPsPoll(); }
            break;
        }
        default:
            reader.skip(sizeof(ElementHeader) + hdr->len);
            break;
        }
    }

done_iter:
    return ZX_OK;
}

zx_status_t Station::HandleAuthentication(const Packet* packet) {
    debugfn();

    if (state_ != WlanState::kUnauthenticated) {
        // TODO(tkilbourn): should we process this Authentication packet anyway? The spec is
        // unclear.
        debugjoin("unexpected authentication frame\n");
        return ZX_OK;
    }

    MacAddr bssid(bss_->bssid.data());
    auto hdr = packet->field<MgmtFrameHeader>(0);
    ZX_DEBUG_ASSERT(hdr->fc.subtype() == ManagementSubtype::kAuthentication);
    ZX_DEBUG_ASSERT(hdr->addr3 == bssid);

    auto auth = packet->field<Authentication>(sizeof(MgmtFrameHeader));
    if (!auth) {
        errorf("authentication packet too small (len=%zd)\n",
               packet->len() - sizeof(MgmtFrameHeader));
        return ZX_ERR_IO;
    }

    if (auth->auth_algorithm_number != auth_alg_) {
        errorf("mismatched authentication algorithm (expected %u, got %u)\n", auth_alg_,
               auth->auth_algorithm_number);
        return ZX_ERR_BAD_STATE;
    }

    // TODO(tkilbourn): this only makes sense for Open System.
    if (auth->auth_txn_seq_number != 2) {
        errorf("unexpected auth txn sequence number (expected 2, got %u)\n",
               auth->auth_txn_seq_number);
        return ZX_ERR_BAD_STATE;
    }

    if (auth->status_code != status_code::kSuccess) {
        errorf("authentication failed (status code=%u)\n", auth->status_code);
        // TODO(tkilbourn): is this the right result code?
        SendAuthResponse(AuthenticateResultCodes::AUTHENTICATION_REJECTED);
        return ZX_ERR_BAD_STATE;
    }

    debugjoin("authenticated to %s\n", MACSTR(bssid));
    state_ = WlanState::kAuthenticated;
    auth_timeout_ = 0;
    timer_->CancelTimer();
    SendAuthResponse(AuthenticateResultCodes::SUCCESS);
    return ZX_OK;
}

zx_status_t Station::HandleDeauthentication(const Packet* packet) {
    debugfn();

    if (state_ != WlanState::kAssociated || state_ != WlanState::kAuthenticated) {
        debugjoin("got spurious deauthenticate; ignoring\n");
        return ZX_OK;
    }

    MacAddr bssid(bss_->bssid.data());
    auto hdr = packet->field<MgmtFrameHeader>(0);
    ZX_DEBUG_ASSERT(hdr->fc.subtype() == ManagementSubtype::kDeauthentication);
    ZX_DEBUG_ASSERT(hdr->addr3 == bssid);

    auto deauth = packet->field<Deauthentication>(sizeof(MgmtFrameHeader));
    if (!deauth) {
        errorf("deauthentication packet too small len=%zu\n", packet->len());
        return ZX_ERR_IO;
    }
    infof("deauthenticating from %s, reason=%u\n", bss_->ssid.data(), deauth->reason_code);

    state_ = WlanState::kAuthenticated;
    return SendDeauthIndication(deauth->reason_code);
}

zx_status_t Station::HandleAssociationResponse(const Packet* packet) {
    debugfn();

    if (state_ != WlanState::kAuthenticated) {
        // TODO(tkilbourn): should we process this Association response packet anyway? The spec is
        // unclear.
        debugjoin("unexpected association response frame\n");
        return ZX_OK;
    }

    MacAddr bssid(bss_->bssid.data());
    auto hdr = packet->field<MgmtFrameHeader>(0);
    ZX_DEBUG_ASSERT(hdr->fc.subtype() == ManagementSubtype::kAssociationResponse);
    ZX_DEBUG_ASSERT(hdr->addr3 == bssid);

    auto assoc = packet->field<AssociationResponse>(sizeof(MgmtFrameHeader));
    if (!assoc) {
        errorf("association response packet too small (len=%zd)\n",
               packet->len() - sizeof(MgmtFrameHeader));
        return ZX_ERR_IO;
    }

    if (assoc->status_code != status_code::kSuccess) {
        errorf("association failed (status code=%u)\n", assoc->status_code);
        // TODO(tkilbourn): map to the correct result code
        SendAssocResponse(AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        return ZX_ERR_BAD_STATE;
    }

    debugjoin("associated with %s\n", MACSTR(bssid));
    state_ = WlanState::kAssociated;
    assoc_timeout_ = 0;
    aid_ = assoc->aid & kAidMask;
    timer_->CancelTimer();
    // TODO(hahnr): For RSNs, only set link status after successfully authenticating with the RSN.
    device_->SetStatus(ETH_STATUS_ONLINE);
    SendAssocResponse(AssociateResultCodes::SUCCESS);

    signal_report_timeout_ = deadline_after_bcn_period(kSignalReportTimeoutTu);
    timer_->SetTimer(signal_report_timeout_);
    auto rxinfo = packet->ctrl_data<wlan_rx_info_t>();
    ZX_DEBUG_ASSERT(rxinfo);
    avg_rssi_.reset();
    avg_rssi_.add(rxinfo->rssi);
    SendSignalReportIndication(rxinfo->rssi);

    // Open port if user connected to an open network.
    if (bss_->rsn.is_null()) {
        debugjoin("802.1X controlled port is now open\n");
        controlled_port_ = PortState::kOpen;
    }

    std::printf("associated\n");

    return ZX_OK;
}

zx_status_t Station::HandleDisassociation(const Packet* packet) {
    debugfn();

    if (state_ != WlanState::kAssociated) {
        debugjoin("got spurious disassociate; ignoring\n");
        return ZX_OK;
    }

    auto hdr = packet->field<MgmtFrameHeader>(0);
    MacAddr bssid(bss_->bssid.data());
    ZX_DEBUG_ASSERT(hdr->fc.subtype() == ManagementSubtype::kDisassociation);
    ZX_DEBUG_ASSERT(hdr->addr3 == bssid);

    auto disassoc = packet->field<Disassociation>(sizeof(MgmtFrameHeader));
    if (!disassoc) {
        errorf("disassociation packet too small len=%zu\n", packet->len());
        return ZX_ERR_IO;
    }
    infof("disassociating from %s(%s), reason=%u\n", MACSTR(bssid), bss_->ssid.data(),
          disassoc->reason_code);

    state_ = WlanState::kAuthenticated;
    device_->SetStatus(0);

    signal_report_timeout_ = 0;
    timer_->CancelTimer();

    return SendDisassociateIndication(disassoc->reason_code);
}

zx_status_t Station::HandleData(const Packet* packet) {
    if (state_ != WlanState::kAssociated) {
        // Drop packets when not associated
        debugf("dropping data packet while not associated\n");
        return ZX_OK;
    }

    // Take signal strength into account.
    auto rxinfo = packet->ctrl_data<wlan_rx_info_t>();
    ZX_DEBUG_ASSERT(rxinfo);
    avg_rssi_.add(rxinfo->rssi);

    // DataFrameHeader was also parsed by MLME so this should not fail
    auto hdr = packet->field<DataFrameHeader>(0);
    ZX_DEBUG_ASSERT(hdr != nullptr);
    debughdr("Frame control: %04x  duration: %u  seq: %u frag: %u\n", hdr->fc.val(), hdr->duration,
             hdr->sc.seq(), hdr->sc.frag());

    const MacAddr& dest = hdr->addr1;
    const MacAddr& bssid = hdr->addr2;
    const MacAddr& src = hdr->addr3;

    debughdr("dest: %s bssid: %s source: %s\n", MACSTR(dest), MACSTR(bssid), MACSTR(src));

    if (hdr->fc.subtype() != 0) {
        warnf("unsupported data subtype %02x\n", hdr->fc.subtype());
        return ZX_OK;
    }

    auto llc = packet->field<LlcHeader>(sizeof(DataFrameHeader));
    if (llc == nullptr) {
        errorf("short data packet len=%zu\n", packet->len());
        return ZX_ERR_IO;
    }
    ZX_DEBUG_ASSERT(packet->len() >= kDataPayloadHeader);

    // Forward EAPOL frames to SME.
    if (be16toh(llc->protocol_id) == kEapolProtocolId) {
        size_t offset = sizeof(DataFrameHeader) + sizeof(LlcHeader);
        auto eapol = packet->field<EapolFrame>(offset);
        if (eapol == nullptr) { return ZX_OK; }
        uint16_t actual_body_len = packet->len() - (offset + sizeof(EapolFrame));
        uint16_t expected_body_len = be16toh(eapol->packet_body_length);
        if (actual_body_len >= expected_body_len) {
            SendEapolIndication(eapol, hdr->addr3, hdr->addr1);
        }
        return ZX_OK;
    }

    // Drop packets if RSN was not yet authorized.
    if (controlled_port_ == PortState::kBlocked) { return ZX_OK; }

    // PS-POLL if there are more buffered unicast frames.
    if (hdr->fc.more_data() && hdr->addr1.IsUcast()) { SendPsPoll(); }

    const size_t eth_len = packet->len() - kDataPayloadHeader + sizeof(EthernetII);
    auto buffer = GetBuffer(eth_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto eth_packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), eth_len));
    // no need to clear the packet since every byte is overwritten
    eth_packet->set_peer(Packet::Peer::kEthernet);
    auto eth = eth_packet->mut_field<EthernetII>(0);

    eth->dest = hdr->addr1;
    eth->src = hdr->addr3;

    eth->ether_type = llc->protocol_id;
    std::memcpy(eth->payload, llc->payload, packet->len() - kDataPayloadHeader);

    zx_status_t status = device_->SendEthernet(std::move(eth_packet));
    if (status != ZX_OK) { errorf("could not send ethernet data: %d\n", status); }
    return status;
}

zx_status_t Station::HandleEth(const Packet* packet) {
    if (state_ != WlanState::kAssociated) {
        // Drop packets when not associated
        debugf("dropping eth packet while not associated\n");
        return ZX_OK;
    }

    auto eth = packet->field<EthernetII>(0);
    if (eth == nullptr) {
        errorf("bad ethernet frame len=%zu\n", packet->len());
        return ZX_ERR_IO;
    }

    const size_t wlan_len = packet->len() - sizeof(EthernetII) + kDataPayloadHeader;
    auto buffer = GetBuffer(wlan_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto wlan_packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), wlan_len));
    // no need to clear the whole packet; we memset the headers instead and copy over all bytes in
    // the payload
    wlan_packet->set_peer(Packet::Peer::kWlan);
    auto hdr = wlan_packet->mut_field<DataFrameHeader>(0);
    std::memset(hdr, 0, sizeof(DataFrameHeader));
    hdr->fc.set_type(kData);
    hdr->fc.set_to_ds(1);

    hdr->addr1 = MacAddr(bss_->bssid.data());
    hdr->addr2 = eth->src;
    hdr->addr3 = eth->dest;

    hdr->sc.set_seq(next_seq());
    debughdr("Frame control: %04x  duration: %u  seq: %u frag: %u\n", hdr->fc.val(), hdr->duration,
             hdr->sc.seq(), hdr->sc.frag());

    // TODO(porce): Fix this inconsistency. See how addr1,2,3 are assigned.
    debughdr("dest: %s source: %s bssid: %s\n", MACSTR(hdr->addr1), MACSTR(hdr->addr2),
             MACSTR(hdr->addr3));

    auto llc = wlan_packet->mut_field<LlcHeader>(sizeof(DataFrameHeader));
    llc->dsap = kLlcSnapExtension;
    llc->ssap = kLlcSnapExtension;
    llc->control = kLlcUnnumberedInformation;
    std::memcpy(llc->oui, kLlcOui, sizeof(llc->oui));
    llc->protocol_id = eth->ether_type;

    std::memcpy(llc->payload, eth->payload, packet->len() - sizeof(EthernetII));

    zx_status_t status = device_->SendWlan(std::move(wlan_packet));
    if (status != ZX_OK) { errorf("could not send wlan data: %d\n", status); }
    return status;
}

zx_status_t Station::HandleTimeout() {
    debugfn();
    zx_time_t now = timer_->Now();
    if (join_timeout_ > 0 && now > join_timeout_) {
        debugjoin("join timed out; resetting\n");

        Reset();
        return SendJoinResponse();
    }

    if (auth_timeout_ > 0 && now >= auth_timeout_) {
        debugjoin("auth timed out; moving back to joining\n");
        auth_timeout_ = 0;
        return SendAuthResponse(AuthenticateResultCodes::AUTH_FAILURE_TIMEOUT);
    }

    if (assoc_timeout_ > 0 && now >= assoc_timeout_) {
        debugjoin("assoc timed out; moving back to authenticated\n");
        assoc_timeout_ = 0;
        // TODO(tkilbourn): need a better error code for this
        return SendAssocResponse(AssociateResultCodes::REFUSED_TEMPORARILY);
    }

    if (signal_report_timeout_ > 0 && now > signal_report_timeout_ &&
        state_ == WlanState::kAssociated) {
        signal_report_timeout_ = deadline_after_bcn_period(kSignalReportTimeoutTu);
        timer_->SetTimer(signal_report_timeout_);
        SendSignalReportIndication(avg_rssi_.avg());
    }

    return ZX_OK;
}

zx_status_t Station::SendJoinResponse() {
    debugfn();
    auto resp = JoinResponse::New();
    resp->result_code = state_ == WlanState::kUnjoined ? JoinResultCodes::JOIN_FAILURE_TIMEOUT
                                                       : JoinResultCodes::SUCCESS;

    size_t buf_len = sizeof(ServiceHeader) + resp->GetSerializedSize();
    fbl::unique_ptr<Buffer> buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), buf_len));
    packet->set_peer(Packet::Peer::kService);
    zx_status_t status = SerializeServiceMsg(packet.get(), Method::JOIN_confirm, resp);
    if (status != ZX_OK) {
        errorf("could not serialize JoinResponse: %d\n", status);
    } else {
        status = device_->SendService(std::move(packet));
    }

    return status;
}

zx_status_t Station::SendAuthResponse(AuthenticateResultCodes code) {
    debugfn();
    auto resp = AuthenticateResponse::New();
    resp->peer_sta_address = fidl::Array<uint8_t>::New(kMacAddrLen);

    MacAddr bssid(bss_->bssid.data());
    bssid.CopyTo(resp->peer_sta_address.data());
    // TODO(tkilbourn): set this based on the actual auth type
    resp->auth_type = AuthenticationTypes::OPEN_SYSTEM;
    resp->result_code = code;

    size_t buf_len = sizeof(ServiceHeader) + resp->GetSerializedSize();
    fbl::unique_ptr<Buffer> buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), buf_len));
    packet->set_peer(Packet::Peer::kService);
    zx_status_t status = SerializeServiceMsg(packet.get(), Method::AUTHENTICATE_confirm, resp);
    if (status != ZX_OK) {
        errorf("could not serialize AuthenticateResponse: %d\n", status);
    } else {
        status = device_->SendService(std::move(packet));
    }

    return status;
}

zx_status_t Station::SendDeauthIndication(uint16_t code) {
    debugfn();
    auto ind = DeauthenticateIndication::New();
    ind->peer_sta_address = fidl::Array<uint8_t>::New(kMacAddrLen);
    MacAddr bssid(bss_->bssid.data());
    bssid.CopyTo(ind->peer_sta_address.data());
    ind->reason_code = code;

    size_t buf_len = sizeof(ServiceHeader) + ind->GetSerializedSize();
    fbl::unique_ptr<Buffer> buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), buf_len));
    packet->set_peer(Packet::Peer::kService);
    zx_status_t status = SerializeServiceMsg(packet.get(), Method::DEAUTHENTICATE_indication, ind);
    if (status != ZX_OK) {
        errorf("could not serialize DeauthenticateIndication: %d\n", status);
    } else {
        status = device_->SendService(std::move(packet));
    }

    return status;
}

zx_status_t Station::SendAssocResponse(AssociateResultCodes code) {
    debugfn();
    auto resp = AssociateResponse::New();
    resp->result_code = code;
    resp->association_id = aid_;

    size_t buf_len = sizeof(ServiceHeader) + resp->GetSerializedSize();
    fbl::unique_ptr<Buffer> buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), buf_len));
    packet->set_peer(Packet::Peer::kService);
    zx_status_t status = SerializeServiceMsg(packet.get(), Method::ASSOCIATE_confirm, resp);
    if (status != ZX_OK) {
        errorf("could not serialize AssociateResponse: %d\n", status);
    } else {
        status = device_->SendService(std::move(packet));
    }

    return status;
}

zx_status_t Station::SendDisassociateIndication(uint16_t code) {
    debugfn();
    auto ind = DisassociateIndication::New();
    ind->peer_sta_address = fidl::Array<uint8_t>::New(kMacAddrLen);
    MacAddr bssid(bss_->bssid.data());
    bssid.CopyTo(ind->peer_sta_address.data());
    ind->reason_code = code;

    size_t buf_len = sizeof(ServiceHeader) + ind->GetSerializedSize();
    fbl::unique_ptr<Buffer> buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), buf_len));
    packet->set_peer(Packet::Peer::kService);
    zx_status_t status = SerializeServiceMsg(packet.get(), Method::DISASSOCIATE_indication, ind);
    if (status != ZX_OK) {
        errorf("could not serialize DisassociateIndication: %d\n", status);
    } else {
        status = device_->SendService(std::move(packet));
    }

    return status;
}

zx_status_t Station::SendSignalReportIndication(uint8_t rssi) {
    debugfn();
    if (state_ != WlanState::kAssociated) { return ZX_OK; }

    auto ind = SignalReportIndication::New();
    ind->rssi = rssi;

    size_t buf_len = sizeof(ServiceHeader) + ind->GetSerializedSize();
    fbl::unique_ptr<Buffer> buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), buf_len));
    packet->set_peer(Packet::Peer::kService);
    zx_status_t status = SerializeServiceMsg(packet.get(), Method::SIGNAL_REPORT_indication, ind);
    if (status != ZX_OK) {
        errorf("could not serialize SignalReportIndication: %d\n", status);
    } else {
        status = device_->SendService(std::move(packet));
    }

    return status;
}

zx_status_t Station::SendEapolRequest(EapolRequestPtr req) {
    debugfn();

    ZX_DEBUG_ASSERT(!req.is_null());
    if (!bss_) { return ZX_ERR_BAD_STATE; }
    if (state_ != WlanState::kAssociated) {
        debugf("dropping MLME-EAPOL.request while not being associated. STA in state %d\n", state_);
        return ZX_OK;
    }

    size_t len = sizeof(DataFrameHeader) + sizeof(LlcHeader) + req->data.size();
    fbl::unique_ptr<Buffer> buffer = GetBuffer(len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }
    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), len));
    packet->clear();
    packet->set_peer(Packet::Peer::kWlan);
    auto hdr = packet->mut_field<DataFrameHeader>(0);
    hdr->fc.set_type(kData);
    hdr->fc.set_to_ds(1);

    // TODO(hahnr): Address 1 should be the BSSID as well, however, our setup somehow is not able
    // to send such packets. Sending 0xFF...FF is just a dirty work around until the actual problem
    // is fixed.
    hdr->addr1.Set(kBcastMac);
    hdr->addr2.Set(req->src_addr.data());
    hdr->addr3.Set(req->dst_addr.data());

    hdr->sc.set_seq(device_->GetState()->next_seq());

    auto llc = packet->mut_field<LlcHeader>(sizeof(DataFrameHeader));
    llc->dsap = kLlcSnapExtension;
    llc->ssap = kLlcSnapExtension;
    llc->control = kLlcUnnumberedInformation;
    std::memcpy(llc->oui, kLlcOui, sizeof(llc->oui));
    llc->protocol_id = htobe16(kEapolProtocolId);
    std::memcpy(llc->payload, req->data.data(), req->data.size());

    zx_status_t status = device_->SendWlan(std::move(packet));
    if (status != ZX_OK) {
        errorf("could not send eapol request packet: %d\n", status);
        SendEapolResponse(EapolResultCodes::TRANSMISSION_FAILURE);
        return status;
    }

    SendEapolResponse(EapolResultCodes::SUCCESS);

    return status;
}

zx_status_t Station::SendEapolResponse(EapolResultCodes result_code) {
    debugfn();

    auto resp = EapolResponse::New();
    resp->result_code = result_code;

    size_t buf_len = sizeof(ServiceHeader) + resp->GetSerializedSize();
    fbl::unique_ptr<Buffer> buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), buf_len));
    packet->set_peer(Packet::Peer::kService);
    zx_status_t status = SerializeServiceMsg(packet.get(), Method::EAPOL_confirm, resp);
    if (status != ZX_OK) {
        errorf("could not serialize EapolResponse: %d\n", status);
    } else {
        status = device_->SendService(std::move(packet));
    }

    return status;
}

zx_status_t Station::SendEapolIndication(const EapolFrame* eapol, const MacAddr& src,
                                         const MacAddr& dst) {
    debugfn();

    // Limit EAPOL packet size. The EAPOL packet's size depends on the link transport protocol and
    // might exceed 255 octets. However, we don't support EAP yet and EAPOL Key frames are always
    // shorter.
    // TODO(hahnr): If necessary, find a better upper bound once we support EAP.
    size_t len = sizeof(EapolFrame) + be16toh(eapol->packet_body_length);
    if (len > 255) { return ZX_OK; }

    auto ind = EapolIndication::New();
    ind->data = ::fidl::Array<uint8_t>::New(len);
    std::memcpy(ind->data.data(), eapol, len);
    ind->src_addr = fidl::Array<uint8_t>::New(kMacAddrLen);
    ind->dst_addr = fidl::Array<uint8_t>::New(kMacAddrLen);
    src.CopyTo(ind->src_addr.data());
    dst.CopyTo(ind->dst_addr.data());

    size_t buf_len = sizeof(ServiceHeader) + ind->GetSerializedSize();
    fbl::unique_ptr<Buffer> buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), buf_len));
    packet->set_peer(Packet::Peer::kService);
    zx_status_t status = SerializeServiceMsg(packet.get(), Method::EAPOL_indication, ind);
    if (status != ZX_OK) {
        errorf("could not serialize EapolIndication: %d\n", status);
    } else {
        status = device_->SendService(std::move(packet));
    }
    return status;
}

zx_status_t Station::PreChannelChange(wlan_channel_t chan) {
    debugfn();
    if (state_ != WlanState::kAssociated) { return ZX_OK; }

    auto assoc_chan_num = channel().channel_num;
    auto current_chan_num = device_->GetState()->channel().channel_num;
    if (current_chan_num == assoc_chan_num) {
        SetPowerManagementMode(true);
        // TODO(hahnr): start buffering tx packets (not here though)
    }
    return ZX_OK;
}

zx_status_t Station::PostChannelChange() {
    debugfn();
    if (state_ != WlanState::kAssociated) { return ZX_OK; }

    auto assoc_chan_num = channel().channel_num;
    auto current_chan_num = device_->GetState()->channel().channel_num;
    if (current_chan_num == assoc_chan_num) {
        SetPowerManagementMode(false);
        // TODO(hahnr): wait for TIM, and PS-POLL all buffered frames from AP.
    }
    return ZX_OK;
}

zx_status_t Station::SetPowerManagementMode(bool ps_mode) {
    if (state_ != WlanState::kAssociated) {
        warnf("cannot adjust power management before being associated\n");
        return ZX_OK;
    }

    const MacAddr& mymac = device_->GetState()->address();
    size_t len = sizeof(DataFrameHeader);
    fbl::unique_ptr<Buffer> buffer = GetBuffer(len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }
    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), len));
    packet->clear();
    packet->set_peer(Packet::Peer::kWlan);
    auto hdr = packet->mut_field<DataFrameHeader>(0);
    hdr->fc.set_type(kData);
    hdr->fc.set_subtype(kNull);
    hdr->fc.set_pwr_mgmt(ps_mode);
    hdr->fc.set_to_ds(1);

    MacAddr bssid(bss_->bssid.data());
    hdr->addr1 = bssid;
    hdr->addr2 = mymac;
    hdr->addr3 = bssid;

    uint16_t seq = device_->GetState()->next_seq();
    hdr->sc.set_seq(seq);

    zx_status_t status = device_->SendWlan(std::move(packet));
    if (status != ZX_OK) {
        errorf("could not send power management packet: %d\n", status);
        return status;
    }
    return ZX_OK;
}

zx_status_t Station::SendPsPoll() {
    // TODO(hahnr): We should probably wait for an RSNA if the network is an
    // RSN. Else we cannot work with the incoming data frame.
    if (state_ != WlanState::kAssociated) {
        warnf("cannot send ps-poll before being associated\n");
        return ZX_OK;
    }

    const MacAddr& mymac = device_->GetState()->address();
    size_t len = sizeof(PsPollFrame);
    fbl::unique_ptr<Buffer> buffer = GetBuffer(len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }
    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), len));
    packet->clear();
    packet->set_peer(Packet::Peer::kWlan);
    auto frame = packet->mut_field<PsPollFrame>(0);
    frame->fc.set_type(kControl);
    frame->fc.set_subtype(kPsPoll);
    frame->aid = aid_;

    frame->bssid = MacAddr(bss_->bssid.data());
    frame->ta = mymac;

    zx_status_t status = device_->SendWlan(std::move(packet));
    if (status != ZX_OK) {
        errorf("could not send power management packet: %d\n", status);
        return status;
    }
    return ZX_OK;
}

uint16_t Station::next_seq() {
    uint16_t seq = device_->GetState()->next_seq();
    if (seq == last_seq_) {
        // If the sequence number has rolled over and back to the last seq number we sent to this
        // station, increment again.
        // IEEE Std 802.11-2016, 10.3.2.11.2, Table 10-3, Note TR1
        seq = device_->GetState()->next_seq();
    }
    last_seq_ = seq;
    return seq;
}

zx_time_t Station::deadline_after_bcn_period(zx_duration_t tus) {
    ZX_DEBUG_ASSERT(!bss_.is_null());
    return timer_->Now() + WLAN_TU(bss_->beacon_period * tus);
}

bool Station::IsHTReady() const {
    // TODO(porce): Placeholder.
    // bool bss_is_ht_capable = true;
    // bool client_is_ht_capable = true;
    // bool client_is_ht_config = true;
    // return bss_is_ht_capable && client_is_ht_capable && client_is_ht_config;

    // TODO(porce): GoogleGuest-Legacy fails to offer DHCP upon
    // return true;
    return false;
}

HtCapabilities Station::BuildHtCapabilities() const {
    // TODO(porce): Find intersection of
    // - BSS capabilities
    // - Client radio capabilities
    // - Client configuration

    // Static cooking for Proof-of-Concept
    HtCapabilities htc;
    HtCapabilityInfo& hci = htc.ht_cap_info;

    hci.set_ldpc_coding_cap(1);
    hci.set_chan_width_set(HtCapabilityInfo::TWENTY_ONLY);
    // hci.set_chan_width_set(HtCapabilityInfo::TWENTY_FORTY);
    hci.set_sm_power_save(HtCapabilityInfo::DISABLED);
    hci.set_greenfield(0);
    hci.set_short_gi_20(1);
    hci.set_short_gi_40(0);
    hci.set_tx_stbc(1);
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

}  // namespace wlan
