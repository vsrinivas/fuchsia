// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/ap/remote_client.h>

#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>

namespace wlan {

#define LOG_STATE_TRANSITION(addr, from, to) \
    debugbss("[client] [%s] %s -> %s\n", addr.ToString().c_str(), from, to);

// BaseState implementation.

template <typename S, typename... Args> void BaseState::MoveToState(Args&&... args) {
    static_assert(fbl::is_base_of<BaseState, S>::value, "State class must implement BaseState");
    client_->MoveToState(fbl::make_unique<S>(client_, std::forward<Args>(args)...));
}

// DeauthenticatedState implementation.

DeauthenticatedState::DeauthenticatedState(RemoteClient* client) : BaseState(client) {}

zx_status_t DeauthenticatedState::HandleAuthentication(
    const ImmutableMgmtFrame<Authentication>& frame, const wlan_rx_info_t& rxinfo) {
    debugfn();
    ZX_DEBUG_ASSERT(frame.hdr->addr2 == client_->addr());
    debugbss("[client] [%s] received Authentication request...\n",
             client_->addr().ToString().c_str());

    auto auth_alg = frame.body->auth_algorithm_number;
    if (auth_alg != AuthAlgorithm::kOpenSystem) {
        errorf("[client] [%s] received auth attempt with unsupported algorithm: %u\n",
               client_->addr().ToString().c_str(), auth_alg);
        return client_->SendAuthentication(status_code::kUnsupportedAuthAlgorithm);
    }

    auto auth_txn_seq_no = frame.body->auth_txn_seq_number;
    if (auth_txn_seq_no != 1) {
        errorf("[client] [%s] received auth attempt with invalid tx seq no: %u\n",
               client_->addr().ToString().c_str(), auth_txn_seq_no);
        return client_->SendAuthentication(status_code::kRefused);
    }

    auto status = client_->SendAuthentication(status_code::kSuccess);
    if (status == ZX_OK) {
        MoveToState<AuthenticatedState>();
        LOG_STATE_TRANSITION(client_->addr(), "Deauthenticated", "Authenticated");
    }
    return status;
}

// AuthenticatedState implementation.

AuthenticatedState::AuthenticatedState(RemoteClient* client) : BaseState(client) {}

void AuthenticatedState::OnEnter() {
    // Start timeout and wait for Association requests.
    auth_timeout_ = client_->CreateTimerDeadline(kAuthenticationTimeoutTu);
    client_->StartTimer(auth_timeout_);
}

void AuthenticatedState::OnExit() {
    client_->CancelTimer();
    auth_timeout_ = zx::time();
}

void AuthenticatedState::HandleTimeout() {
    if (client_->IsDeadlineExceeded(auth_timeout_)) {
        MoveToState<DeauthenticatedState>();
        LOG_STATE_TRANSITION(client_->addr(), "Authenticated", "Deauthenticated");
    }
}

zx_status_t AuthenticatedState::HandleAuthentication(
    const ImmutableMgmtFrame<Authentication>& frame, const wlan_rx_info_t& rxinfo) {
    debugbss("[client] [%s] received Authentication request while being authenticated\n",
             client_->addr().ToString().c_str());
    // Client is already authenticated but seems to not have received the previous Authentication
    // response which was sent. Hence, let the client know its authentication was successful.
    // TODO(hahnr): We should process the authentication frame again?
    return client_->SendAuthentication(status_code::kSuccess);
}

zx_status_t AuthenticatedState::HandleDeauthentication(
    const ImmutableMgmtFrame<Deauthentication>& frame, const wlan_rx_info_t& rxinfo) {
    debugbss("[client] [%s] received Deauthentication request: %u\n",
             client_->addr().ToString().c_str(), frame.body->reason_code);
    MoveToState<DeauthenticatedState>();
    LOG_STATE_TRANSITION(client_->addr(), "Authenticated", "Deauthenticated");
    return ZX_OK;
}

zx_status_t AuthenticatedState::HandleAssociationRequest(
    const ImmutableMgmtFrame<AssociationRequest>& frame, const wlan_rx_info_t& rxinfo) {
    debugfn();
    ZX_DEBUG_ASSERT(frame.hdr->addr2 == client_->addr());
    debugbss("[client] [%s] received Assocation Request\n", client_->addr().ToString().c_str());

    // Received request which we've been waiting for. Timer can get canceled.
    client_->CancelTimer();
    auth_timeout_ = zx::time();

    aid_t aid;
    auto status = client_->bss()->AssignAid(client_->addr(), &aid);
    if (status == ZX_ERR_NO_RESOURCES) {
        debugbss("[client] [%s] no more AIDs available \n", client_->addr().ToString().c_str());
        client_->SendAssociationResponse(0, status_code::kDeniedNoMoreStas);
        // TODO(hahnr): Unclear whether we should deauth the client or not. Check existing AP
        // implementations for their behavior. For now, let the client stay authenticated.
        return ZX_OK;
    } else if (status != ZX_OK) {
        errorf("[client] [%s] couldn't assign AID to client: %d\n",
               client_->addr().ToString().c_str(), status);
        return ZX_OK;
    }

    // TODO(hahnr): Send MLME-Authenticate.indication and wait for response.
    // For now simply send association response.
    client_->SendAssociationResponse(aid, status_code::kSuccess);
    MoveToState<AssociatedState>(aid);
    LOG_STATE_TRANSITION(client_->addr(), "Authenticated", "Associated");
    return status;
}

// AssociatedState implementation.

AssociatedState::AssociatedState(RemoteClient* client, uint16_t aid)
    : BaseState(client), aid_(aid) {}

zx_status_t AssociatedState::HandleAssociationRequest(
    const ImmutableMgmtFrame<AssociationRequest>& frame, const wlan_rx_info_t& rxinfo) {
    debugfn();
    ZX_DEBUG_ASSERT(frame.hdr->addr2 == client_->addr());
    debugbss("[client] [%s] received Assocation Request while being associated\n",
             client_->addr().ToString().c_str());

    // Even though the client is already associated, Association requests should still be answered.
    // This can happen when the client for some reasons did not receive the previous
    // AssociationResponse the BSS sent and keeps sending Association requests.
    return client_->SendAssociationResponse(aid_, status_code::kSuccess);
}

void AssociatedState::OnEnter() {
    debugbss("[client] [%s] acquired AID: %u\n", client_->addr().ToString().c_str(), aid_);

    inactive_timeout_ = client_->CreateTimerDeadline(kInactivityTimeoutTu);
    client_->StartTimer(inactive_timeout_);
    debugbss("[client] [%s] started inactivity timer\n", client_->addr().ToString().c_str());
}

zx_status_t AssociatedState::HandleEthFrame(const ImmutableBaseFrame<EthernetII>& frame) {
    if (dozing_) {
        // Enqueue ethernet frame and postpone conversion to when the frame is sent to the client.
        auto status = client_->EnqueueEthernetFrame(frame);
        if (status == ZX_ERR_NO_RESOURCES) {
            debugbss("[client] [%s] reached PS buffering limit; dropping frame\n",
                     client_->addr().ToString().c_str());
        } else if (status != ZX_OK) {
            errorf("[client] couldn't enqueue ethernet frame: %d\n", status);
        }
        return status;
    }

    // If the client is awake and not in power saving mode, convert and send frame immediately.
    fbl::unique_ptr<Packet> out_frame;
    auto status = client_->ConvertEthernetToDataFrame(frame, &out_frame);
    if (status != ZX_OK) {
        errorf("[client] couldn't convert ethernet frame: %d\n", status);
        return status;
    }
    return client_->SendDataFrame(fbl::move(out_frame));
}

zx_status_t AssociatedState::HandleDeauthentication(
    const ImmutableMgmtFrame<Deauthentication>& frame, const wlan_rx_info_t& rxinfo) {
    debugbss("[client] [%s] received Deauthentication request: %u\n",
             client_->addr().ToString().c_str(), frame.body->reason_code);
    MoveToState<DeauthenticatedState>();
    LOG_STATE_TRANSITION(client_->addr(), "Associated", "Deauthenticated");
    return ZX_OK;
}

zx_status_t AssociatedState::HandleDisassociation(const ImmutableMgmtFrame<Disassociation>& frame,
                                                  const wlan_rx_info_t& rxinfo) {
    debugbss("[client] [%s] received Disassociation request: %u\n",
             client_->addr().ToString().c_str(), frame.body->reason_code);
    MoveToState<AuthenticatedState>();
    LOG_STATE_TRANSITION(client_->addr(), "Associated", "Authenticated");
    return ZX_OK;
}

zx_status_t AssociatedState::HandleCtrlFrame(const FrameControl& fc) {
    UpdatePowerSaveMode(fc);
    return ZX_OK;
}

zx_status_t AssociatedState::HandlePsPollFrame(const ImmutableCtrlFrame<PsPollFrame>& frame,
                                               const wlan_rx_info_t& rxinfo) {
    if (client_->HasBufferedFrames()) {
        // Dequeue buffered Ethernet frame.
        fbl::unique_ptr<Packet> packet;
        auto status = client_->DequeueEthernetFrame(&packet);
        if (status != ZX_OK) {
            errorf("[client] [%s] unable to dequeue buffered frames\n",
                   client_->addr().ToString().c_str());
            return status;
        }

        // Treat Packet as Ethernet frame.
        auto hdr = packet->field<EthernetII>(0);
        auto payload = packet->field<uint8_t>(sizeof(hdr));
        size_t payload_len = packet->len() - sizeof(hdr);
        auto eth_frame = ImmutableBaseFrame<EthernetII>(hdr, payload, payload_len);

        // Convert Ethernet to Data frame.
        fbl::unique_ptr<Packet> data_packet;
        status = client_->ConvertEthernetToDataFrame(eth_frame, &data_packet);
        if (status != ZX_OK) {
            errorf("[client] [%s] couldn't convert ethernet frame: %d\n",
                   client_->addr().ToString().c_str(), status);
            return status;
        }

        // Set `more` bit if there are more frames buffered.
        auto fc = data_packet->mut_field<FrameControl>(0);
        fc->set_more_data(client_->HasBufferedFrames() ? 1 : 0);

        // Send Data frame.
        return client_->SendDataFrame(fbl::move(data_packet));
    }

    // There are no frames buffered for the client.
    // Respond with a null data frame and report the situation.
    size_t len = sizeof(DataFrameHeader);
    fbl::unique_ptr<Buffer> buffer = GetBuffer(len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::make_unique<Packet>(fbl::move(buffer), len);
    packet->clear();
    packet->set_peer(Packet::Peer::kWlan);
    auto hdr = packet->mut_field<DataFrameHeader>(0);
    hdr->fc.set_type(FrameType::kData);
    hdr->fc.set_subtype(DataSubtype::kNull);
    hdr->fc.set_from_ds(1);
    hdr->addr1 = client_->addr();
    hdr->addr2 = client_->bss()->bssid();
    hdr->addr3 = client_->bss()->bssid();
    hdr->sc.set_seq(client_->bss()->NextSeq(*hdr));

    zx_status_t status = client_->SendDataFrame(fbl::move(packet));
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send null data frame as PS-POLL response: %d\n",
               client_->addr().ToString().c_str(), status);
        return status;
    }

    return ZX_OK;
}

void AssociatedState::OnExit() {
    client_->CancelTimer();
    inactive_timeout_ = zx::time();

    // Ensure the client's AID is released when association is broken.
    client_->bss()->ReleaseAid(client_->addr());
    debugbss("[client] [%s] released AID: %u\n", client_->addr().ToString().c_str(), aid_);
}

zx_status_t AssociatedState::HandleDataFrame(const DataFrameHeader& hdr) {
    active_ = true;
    UpdatePowerSaveMode(hdr.fc);
    return ZX_OK;
}

zx_status_t AssociatedState::HandleDataFrame(const ImmutableDataFrame<LlcHeader>& frame,
                                             const wlan_rx_info_t& rxinfo) {
    // Filter unsupported Data frame types.
    switch (frame.hdr->fc.subtype()) {
    case DataSubtype::kDataSubtype:
        // Fall-through
    case DataSubtype::kQosdata:  // For data frames within BlockAck session.
        break;
    default:
        warnf("unsupported data subtype %02x\n", frame.hdr->fc.subtype());
        return ZX_OK;
    }

    if (frame.hdr->fc.to_ds() == 0 || frame.hdr->fc.from_ds() == 1) {
        warnf("received unsupported data frame from %s with to_ds/from_ds combination: %u/%u\n",
              frame.hdr->addr2.ToString().c_str(), frame.hdr->fc.to_ds(), frame.hdr->fc.from_ds());
        return ZX_OK;
    }

    auto hdr = frame.hdr;
    auto llc = frame.body;

    // Forward EAPOL frames to SME.
    if (be16toh(llc->protocol_id) == kEapolProtocolId) {
        if (frame.body_len < sizeof(EapolFrame)) {
            warnf("short EAPOL frame; len = %zu", frame.body_len);
            return ZX_OK;
        }

        auto eapol = reinterpret_cast<const EapolFrame*>(llc->payload);
        uint16_t actual_body_len = frame.body_len;
        uint16_t expected_body_len = be16toh(eapol->packet_body_length);
        if (actual_body_len != expected_body_len) {
            return client_->SendEapolIndication(*eapol, hdr->addr2, hdr->addr3);
        }
        return ZX_OK;
    }

    // TODO(NET-463): Disallow data frames if RSNA is required but not established.
    // TODO(NET-445): Check FC's power saving bit.

    const size_t eth_len = frame.body_len + sizeof(EthernetII);
    auto buffer = GetBuffer(eth_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto eth_packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), eth_len));
    // no need to clear the packet since every byte is overwritten
    eth_packet->set_peer(Packet::Peer::kEthernet);

    auto eth = eth_packet->mut_field<EthernetII>(0);
    eth->dest = hdr->addr3;
    eth->src = hdr->addr2;
    eth->ether_type = llc->protocol_id;
    std::memcpy(eth->payload, llc->payload, frame.body_len - sizeof(LlcHeader));

    auto status = client_->SendEthernet(std::move(eth_packet));
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send ethernet data: %d\n",
               client_->addr().ToString().c_str(), status);
    }

    return status;
}

zx_status_t AssociatedState::HandleMgmtFrame(const MgmtFrameHeader& hdr) {
    active_ = true;
    UpdatePowerSaveMode(hdr.fc);
    return ZX_OK;
}

void AssociatedState::HandleTimeout() {
    if (!client_->IsDeadlineExceeded(inactive_timeout_)) { return; }

    if (active_) {
        active_ = false;

        // Client was active, restart timer.
        debugbss("[client] [%s] client is active; reset inactive timer\n",
                 client_->addr().ToString().c_str());
        inactive_timeout_ = client_->CreateTimerDeadline(kInactivityTimeoutTu);
        client_->StartTimer(inactive_timeout_);
    } else {
        active_ = false;

        // The client timed-out, send Deauthentication. Ignore result, always leave associated
        // state.
        client_->SendDeauthentication(reason_code::ReasonCode::kReasonInactivity);
        debugbss("[client] [%s] client inactive for %lu seconds; deauthenticating client\n",
                 client_->addr().ToString().c_str(), kInactivityTimeoutTu / 1000);
        MoveToState<DeauthenticatedState>();
        LOG_STATE_TRANSITION(client_->addr(), "Associated", "Deauthenticated");
    }
}

void AssociatedState::UpdatePowerSaveMode(const FrameControl& fc) {
    if (fc.pwr_mgmt() != dozing_) {
        dozing_ = fc.pwr_mgmt();

        if (!dozing_) {
            // TODO(hahnr): Client became awake.
            // Send all remaining buffered frames (U-APSD).
        }
    }
}

zx_status_t AssociatedState::HandleMlmeEapolReq(const EapolRequest& req) {
    size_t len = sizeof(DataFrameHeader) + sizeof(LlcHeader) + req.data.size();
    auto buffer = GetBuffer(len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::unique_ptr<Packet>(new Packet(fbl::move(buffer), len));
    packet->clear();
    packet->set_peer(Packet::Peer::kWlan);

    auto hdr = packet->mut_field<DataFrameHeader>(0);
    hdr->fc.set_type(FrameType::kData);
    hdr->fc.set_from_ds(1);
    hdr->addr1.Set(req.dst_addr.data());
    hdr->addr2 = client_->bss()->bssid();
    hdr->addr3.Set(req.src_addr.data());
    hdr->sc.set_seq(client_->bss()->NextSeq(*hdr));

    auto llc = packet->mut_field<LlcHeader>(sizeof(DataFrameHeader));
    llc->dsap = kLlcSnapExtension;
    llc->ssap = kLlcSnapExtension;
    llc->control = kLlcUnnumberedInformation;
    std::memcpy(llc->oui, kLlcOui, sizeof(llc->oui));
    llc->protocol_id = htobe16(kEapolProtocolId);
    std::memcpy(llc->payload, req.data.data(), req.data.size());

    auto status = client_->SendDataFrame(fbl::move(packet));
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send EAPOL request packet: %d\n",
               client_->addr().ToString().c_str(), status);
        client_->SendEapolResponse(EapolResultCodes::TRANSMISSION_FAILURE);
        return status;
    }

    client_->SendEapolResponse(EapolResultCodes::SUCCESS);
    return status;
}

// RemoteClient implementation.

RemoteClient::RemoteClient(DeviceInterface* device, fbl::unique_ptr<Timer> timer, BssInterface* bss,
                           RemoteClient::Listener* listener, const common::MacAddr& addr)
    : listener_(listener), device_(device), bss_(bss), addr_(addr), timer_(std::move(timer)) {
    ZX_DEBUG_ASSERT(device_ != nullptr);
    ZX_DEBUG_ASSERT(timer_ != nullptr);
    ZX_DEBUG_ASSERT(bss_ != nullptr);
    debugbss("[client] [%s] spawned\n", addr_.ToString().c_str());

    MoveToState(fbl::make_unique<DeauthenticatedState>(this));
    LOG_STATE_TRANSITION(addr_, "(init)", "Deauthenticated");
}

RemoteClient::~RemoteClient() {
    debugbss("[client] [%s] destroyed\n", addr_.ToString().c_str());
}

void RemoteClient::MoveToState(fbl::unique_ptr<BaseState> to) {
    auto from_id = state() == nullptr ? StateId::kUninitialized : state()->id();
    if (to == nullptr) {
        errorf("attempt to transition to a nullptr from state: %hhu\n", from_id);
        ZX_DEBUG_ASSERT(to == nullptr);
        return;
    }

    auto to_id = to->id();
    fsm::StateMachine<BaseState>::MoveToState(fbl::move(to));

    if (listener_ != nullptr) {
        listener_->HandleClientStateChange(addr_, from_id, to_id);
    }
}

void RemoteClient::HandleTimeout() {
    state()->HandleTimeout();
}

zx_status_t RemoteClient::HandleDataFrame(const DataFrameHeader& hdr) {
    ZX_DEBUG_ASSERT(hdr.addr2 == addr_);
    if (hdr.addr2 != addr_) { return ZX_ERR_STOP; }

    ForwardCurrentFrameTo(state());
    return ZX_OK;
}

zx_status_t RemoteClient::HandleMgmtFrame(const MgmtFrameHeader& hdr) {
    ZX_DEBUG_ASSERT(hdr.addr2 == addr_);
    if (hdr.addr2 != addr_) { return ZX_ERR_STOP; }

    ForwardCurrentFrameTo(state());
    return ZX_OK;
}

zx_status_t RemoteClient::HandlePsPollFrame(const ImmutableCtrlFrame<PsPollFrame>& frame,
                                            const wlan_rx_info_t& rxinfo) {
    ZX_DEBUG_ASSERT(frame.hdr->ta == addr_);
    if (frame.hdr->ta != addr_) { return ZX_ERR_STOP; }

    ForwardCurrentFrameTo(state());
    return ZX_OK;
}

zx_status_t RemoteClient::StartTimer(zx::time deadline) {
    CancelTimer();
    return timer_->SetTimer(deadline);
}

zx_status_t RemoteClient::CancelTimer() {
    return timer_->CancelTimer();
}

zx::time RemoteClient::CreateTimerDeadline(zx_duration_t tus) {
    return timer_->Now() + WLAN_TU(tus);
}

bool RemoteClient::IsDeadlineExceeded(zx::time deadline) {
    return deadline > zx::time() && timer_->Now() >= deadline;
}

zx_status_t RemoteClient::SendAuthentication(status_code::StatusCode result) {
    debugfn();
    debugbss("[client] [%s] sending Authentication response\n", addr_.ToString().c_str());

    fbl::unique_ptr<Packet> packet = nullptr;
    auto frame = BuildMgmtFrame<Authentication>(&packet);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    FillTxInfo(&packet, *frame.hdr);

    auto hdr = frame.hdr;
    hdr->fc.set_from_ds(1);
    hdr->addr1 = addr_;
    hdr->addr2 = bss_->bssid();
    hdr->addr3 = bss_->bssid();
    hdr->sc.set_seq(bss_->NextSeq(*hdr));

    auto auth = frame.body;
    auth->status_code = result;
    auth->auth_algorithm_number = AuthAlgorithm::kOpenSystem;
    // TODO(hahnr): Evolve this to support other authentication algorithms and track seq number.
    auth->auth_txn_seq_number = 2;

    auto status = device_->SendWlan(fbl::move(packet));
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send auth response packet: %d\n", addr_.ToString().c_str(),
               status);
    }
    return status;
}

zx_status_t RemoteClient::SendAssociationResponse(aid_t aid, status_code::StatusCode result) {
    debugfn();
    debugbss("[client] [%s] sending Association Response\n", addr_.ToString().c_str());

    fbl::unique_ptr<Packet> packet = nullptr;
    auto frame = BuildMgmtFrame<AssociationResponse>(&packet);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto hdr = frame.hdr;
    hdr->fc.set_from_ds(1);
    hdr->addr1 = addr_;
    hdr->addr2 = bss_->bssid();
    hdr->addr3 = bss_->bssid();
    hdr->sc.set_seq(bss_->NextSeq(*hdr));

    auto assoc = frame.body;
    assoc->status_code = result;
    assoc->aid = aid;
    assoc->cap.set_ess(1);
    assoc->cap.set_short_preamble(1);

    auto status = device_->SendWlan(fbl::move(packet));
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send auth response packet: %d\n", addr_.ToString().c_str(),
               status);
    }
    return status;
}

zx_status_t RemoteClient::SendDeauthentication(reason_code::ReasonCode reason_code) {
    debugfn();
    debugbss("[client] [%s] sending Disassociation\n", addr_.ToString().c_str());

    fbl::unique_ptr<Packet> packet = nullptr;
    auto frame = BuildMgmtFrame<Deauthentication>(&packet);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto hdr = frame.hdr;
    hdr->fc.set_from_ds(1);
    hdr->addr1 = addr_;
    hdr->addr2 = bss_->bssid();
    hdr->addr3 = bss_->bssid();
    hdr->sc.set_seq(bss_->NextSeq(*hdr));

    auto deauth = frame.body;
    deauth->reason_code = reason_code;

    auto status = device_->SendWlan(fbl::move(packet));
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send disassocation packet: %d\n", addr_.ToString().c_str(),
               status);
    }
    return status;
}

zx_status_t RemoteClient::SendEthernet(fbl::unique_ptr<Packet> packet) {
    return device_->SendEthernet(fbl::move(packet));
}

zx_status_t RemoteClient::SendDataFrame(fbl::unique_ptr<Packet> packet) {
    return device_->SendWlan(fbl::move(packet));
}

zx_status_t RemoteClient::SendEapolIndication(const EapolFrame& eapol, const common::MacAddr& src,
                                              const common::MacAddr& dst) {
    debugfn();

    // Limit EAPOL packet size. The EAPOL packet's size depends on the link transport protocol and
    // might exceed 255 octets. However, we don't support EAP yet and EAPOL Key frames are always
    // shorter.
    // TODO(hahnr): If necessary, find a better upper bound once we support EAP.
    size_t len = sizeof(EapolFrame) + be16toh(eapol.packet_body_length);
    if (len > 255) { return ZX_OK; }

    auto ind = EapolIndication::New();
    ind->data = ::f1dl::Array<uint8_t>::New(len);
    std::memcpy(ind->data.data(), &eapol, len);
    ind->src_addr = f1dl::Array<uint8_t>::New(common::kMacAddrLen);
    ind->dst_addr = f1dl::Array<uint8_t>::New(common::kMacAddrLen);
    src.CopyTo(ind->src_addr.data());
    dst.CopyTo(ind->dst_addr.data());

    size_t buf_len = sizeof(ServiceHeader) + ind->GetSerializedSize();
    fbl::unique_ptr<Buffer> buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::make_unique<Packet>(fbl::move(buffer), buf_len);
    packet->set_peer(Packet::Peer::kService);
    auto status = SerializeServiceMsg(packet.get(), Method::EAPOL_indication, ind);
    if (status != ZX_OK) {
        errorf("could not serialize MLME-Eapol.indication: %d\n", status);
    } else {
        status = device_->SendService(fbl::move(packet));
    }
    return status;
}

zx_status_t RemoteClient::SendEapolResponse(EapolResultCodes result_code) {
    debugfn();

    auto resp = EapolResponse::New();
    resp->result_code = result_code;

    size_t buf_len = sizeof(ServiceHeader) + resp->GetSerializedSize();
    auto buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::make_unique<Packet>(fbl::move(buffer), buf_len);
    packet->set_peer(Packet::Peer::kService);
    zx_status_t status = SerializeServiceMsg(packet.get(), Method::EAPOL_confirm, resp);
    if (status != ZX_OK) {
        errorf("could not serialize EapolResponse: %d\n", status);
    } else {
        status = device_->SendService(fbl::move(packet));
    }
    return status;
}

zx_status_t RemoteClient::EnqueueEthernetFrame(const ImmutableBaseFrame<EthernetII>& frame) {
    if (ps_pkt_queue_.size() >= kMaxPowerSavingQueueSize) { return ZX_ERR_NO_RESOURCES; }

    size_t hdr_len = sizeof(EthernetII);
    size_t frame_len = hdr_len + frame.body_len;
    auto buffer = bss_->GetPowerSavingBuffer(frame_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    // Copy ethernet frame into buffer acquired from the BSS.
    auto packet = fbl::make_unique<Packet>(std::move(buffer), frame_len);
    memcpy(packet->mut_data(), frame.hdr, frame_len);

    ps_pkt_queue_.Enqueue(fbl::move(packet));
    ReportBuChange(ps_pkt_queue_.size());

    return ZX_OK;
}

zx_status_t RemoteClient::DequeueEthernetFrame(fbl::unique_ptr<Packet>* out_packet) {
    ZX_DEBUG_ASSERT(ps_pkt_queue_.size() > 0);
    if (ps_pkt_queue_.size() == 0) { return ZX_ERR_NO_RESOURCES; }

    *out_packet = ps_pkt_queue_.Dequeue();
    ReportBuChange(ps_pkt_queue_.size());

    return ZX_OK;
}

bool RemoteClient::HasBufferedFrames() const {
    return ps_pkt_queue_.size() > 0;
}

zx_status_t RemoteClient::ConvertEthernetToDataFrame(const ImmutableBaseFrame<EthernetII>& frame,
                                                     fbl::unique_ptr<Packet>* out_packet) {
    const size_t data_frame_len = kDataPayloadHeader + frame.body_len;
    auto buffer = GetBuffer(data_frame_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    *out_packet = fbl::make_unique<Packet>(std::move(buffer), data_frame_len);
    (*out_packet)->set_peer(Packet::Peer::kWlan);

    auto hdr = (*out_packet)->mut_field<DataFrameHeader>(0);
    hdr->fc.clear();
    hdr->fc.set_type(FrameType::kData);
    hdr->fc.set_subtype(DataSubtype::kDataSubtype);
    hdr->fc.set_from_ds(1);
    // TODO(hahnr): Protect outgoing frames when RSNA is established.
    hdr->sc.set_seq(bss_->NextSeq(*hdr));
    hdr->addr1 = frame.hdr->dest;
    hdr->addr2 = bss_->bssid();
    hdr->addr3 = frame.hdr->src;

    auto llc = (*out_packet)->mut_field<LlcHeader>(hdr->len());
    llc->dsap = kLlcSnapExtension;
    llc->ssap = kLlcSnapExtension;
    llc->control = kLlcUnnumberedInformation;
    std::memcpy(llc->oui, kLlcOui, sizeof(llc->oui));
    llc->protocol_id = frame.hdr->ether_type;
    std::memcpy(llc->payload, frame.body, frame.body_len);

    wlan_tx_info_t txinfo = {
        // TODO(hahnr): Fill wlan_tx_info_t.
    };
    (*out_packet)->CopyCtrlFrom(txinfo);

    return ZX_OK;
}

void RemoteClient::ReportBuChange(size_t bu_count) {
    if (listener_ != nullptr) { listener_->HandleClientBuChange(addr_, bu_count); }
}

#undef LOG_STATE_TRANSITION

}  // namespace wlan
