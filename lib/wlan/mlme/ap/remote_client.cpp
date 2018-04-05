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

    // Move into Authenticating state which responds to incoming Authentication request.
    LOG_STATE_TRANSITION(client_->addr(), "Deauthenticated", "Authenticating");
    MoveToState<AuthenticatingState>(frame);
    return ZX_ERR_STOP;
}

// AuthenticatingState implementation.

AuthenticatingState::AuthenticatingState(RemoteClient* client,
                                         const ImmutableMgmtFrame<Authentication>& frame)
    : BaseState(client) {
    ZX_DEBUG_ASSERT(frame.hdr->addr2 == client_->addr());
    debugbss("[client] [%s] received Authentication request...\n",
             client_->addr().ToString().c_str());
    status_code_ = status_code::kRefusedReasonUnspecified;

    auto auth_alg = frame.body->auth_algorithm_number;
    if (auth_alg != AuthAlgorithm::kOpenSystem) {
        errorf("[client] [%s] received auth attempt with unsupported algorithm: %u\n",
               client_->addr().ToString().c_str(), auth_alg);
        status_code_ = status_code::kUnsupportedAuthAlgorithm;
        return;
    }

    auto auth_txn_seq_no = frame.body->auth_txn_seq_number;
    if (auth_txn_seq_no != 1) {
        errorf("[client] [%s] received auth attempt with invalid tx seq no: %u\n",
               client_->addr().ToString().c_str(), auth_txn_seq_no);
        status_code_ = status_code::kRefused;
        return;
    }
    status_code_ = status_code::kSuccess;
}

void AuthenticatingState::OnEnter() {
    bool auth_success = status_code_ == status_code::kSuccess;
    auto status = client_->SendAuthentication(status_code_);
    if (auth_success && status == ZX_OK) {
        LOG_STATE_TRANSITION(client_->addr(), "Authenticating", "Authenticated");
        MoveToState<AuthenticatedState>();
    } else {
        LOG_STATE_TRANSITION(client_->addr(), "Authenticating", "Deauthenticated");
        MoveToState<DeauthenticatedState>();
    }
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
    LOG_STATE_TRANSITION(client_->addr(), "Authenticated", "Authenticating");
    MoveToState<AuthenticatingState>(frame);
    return ZX_ERR_STOP;
}

zx_status_t AuthenticatedState::HandleDeauthentication(
    const ImmutableMgmtFrame<Deauthentication>& frame, const wlan_rx_info_t& rxinfo) {
    debugbss("[client] [%s] received Deauthentication: %u\n", client_->addr().ToString().c_str(),
             frame.body->reason_code);
    MoveToState<DeauthenticatedState>();
    LOG_STATE_TRANSITION(client_->addr(), "Authenticated", "Deauthenticated");
    return ZX_ERR_STOP;
}

zx_status_t AuthenticatedState::HandleAssociationRequest(
    const ImmutableMgmtFrame<AssociationRequest>& frame, const wlan_rx_info_t& rxinfo) {

    // Received request which we've been waiting for. Timer can get canceled.
    client_->CancelTimer();
    auth_timeout_ = zx::time();

    // Move into Associating state state which responds to incoming Association requests.
    LOG_STATE_TRANSITION(client_->addr(), "Authenticated", "Associating");
    MoveToState<AssociatingState>(frame);
    return ZX_ERR_STOP;
}

// AssociatingState implementation.

AssociatingState::AssociatingState(RemoteClient* client,
                                   const ImmutableMgmtFrame<AssociationRequest>& frame)
    : BaseState(client), status_code_(status_code::kRefusedReasonUnspecified), aid_(0) {
    debugfn();
    ZX_DEBUG_ASSERT(frame.hdr->addr2 == client_->addr());
    debugbss("[client] [%s] received Assocation Request\n", client_->addr().ToString().c_str());

    aid_t aid;
    auto status = client_->bss()->AssignAid(client_->addr(), &aid);
    if (status == ZX_ERR_NO_RESOURCES) {
        debugbss("[client] [%s] no more AIDs available \n", client_->addr().ToString().c_str());
        status_code_ = status_code::kDeniedNoMoreStas;
        return;
    } else if (status != ZX_OK) {
        errorf("[client] [%s] couldn't assign AID to client: %d\n",
               client_->addr().ToString().c_str(), status);
        return;
    }

    status_code_ = status_code::kSuccess;
    aid_ = aid;
}

void AssociatingState::OnEnter() {
    // TODO(hahnr): Send MLME-Authenticate.indication and wait for response.
    // For now simply send association response.
    bool assoc_success = (status_code_ == status_code::kSuccess);
    auto status = client_->SendAssociationResponse(aid_, status_code_);
    if (assoc_success && status == ZX_OK) {
        LOG_STATE_TRANSITION(client_->addr(), "AssociatingState", "Associated");
        MoveToState<AssociatedState>(aid_);
    } else {
        LOG_STATE_TRANSITION(client_->addr(), "AssociatingState", "Deauthenticated");
        MoveToState<DeauthenticatedState>();
    }
}

// AssociatedState implementation.

AssociatedState::AssociatedState(RemoteClient* client, uint16_t aid)
    : BaseState(client), aid_(aid) {}

zx_status_t AssociatedState::HandleAuthentication(const ImmutableMgmtFrame<Authentication>& frame,
                                                  const wlan_rx_info_t& rxinfo) {
    debugbss("[client] [%s] received Authentication request while being associated\n",
             client_->addr().ToString().c_str());
    // Client believes it is not yet authenticated. Thus, there is no need to send an explicit
    // Deauthentication.
    req_deauth_ = false;

    LOG_STATE_TRANSITION(client_->addr(), "Associated", "Authenticating");
    MoveToState<AuthenticatingState>(frame);
    return ZX_ERR_STOP;
}

zx_status_t AssociatedState::HandleAssociationRequest(
    const ImmutableMgmtFrame<AssociationRequest>& frame, const wlan_rx_info_t& rxinfo) {
    debugfn();
    ZX_DEBUG_ASSERT(frame.hdr->addr2 == client_->addr());
    debugbss("[client] [%s] received Assocation Request while being associated\n",
             client_->addr().ToString().c_str());
    // Client believes it is not yet associated. Thus, there is no need to send an explicit
    // Deauthentication.
    req_deauth_ = false;

    LOG_STATE_TRANSITION(client_->addr(), "Associated", "Associating");
    MoveToState<AssociatingState>(frame);
    return ZX_ERR_STOP;
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
            debugps("[client] [%s] reached PS buffering limit; dropping frame\n",
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
    return client_->bss()->SendDataFrame(fbl::move(out_frame));
}

zx_status_t AssociatedState::HandleDeauthentication(
    const ImmutableMgmtFrame<Deauthentication>& frame, const wlan_rx_info_t& rxinfo) {
    debugbss("[client] [%s] received Deauthentication: %u\n", client_->addr().ToString().c_str(),
             frame.body->reason_code);
    req_deauth_ = false;
    MoveToState<DeauthenticatedState>();
    LOG_STATE_TRANSITION(client_->addr(), "Associated", "Deauthenticated");
    return ZX_ERR_STOP;
}

zx_status_t AssociatedState::HandleDisassociation(const ImmutableMgmtFrame<Disassociation>& frame,
                                                  const wlan_rx_info_t& rxinfo) {
    debugbss("[client] [%s] received Disassociation request: %u\n",
             client_->addr().ToString().c_str(), frame.body->reason_code);
    MoveToState<AuthenticatedState>();
    LOG_STATE_TRANSITION(client_->addr(), "Associated", "Authenticated");
    return ZX_ERR_STOP;
}

zx_status_t AssociatedState::HandleCtrlFrame(const FrameControl& fc) {
    UpdatePowerSaveMode(fc);
    return ZX_OK;
}

zx_status_t AssociatedState::HandlePsPollFrame(const ImmutableCtrlFrame<PsPollFrame>& frame,
                                               const wlan_rx_info_t& rxinfo) {
    debugbss("[client] [%s] client requested BU\n", client_->addr().ToString().c_str());

    if (client_->HasBufferedFrames()) {
        return SendNextBu();
    }

    debugbss("[client] [%s] no more BU available\n", client_->addr().ToString().c_str());
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

    zx_status_t status = client_->bss()->SendDataFrame(fbl::move(packet));
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

    // Ensure Deauthentication is sent to the client if itself didn't send such notification or such
    // notification wasn't already sent due to inactivity of the client.
    // This Deauthentication is usually issued when the BSS stopped and its associated clients
    // need to get notified.
    if (req_deauth_) {
        req_deauth_ = false;
        debugbss("[client] [%s] ending association; deauthenticating client\n",
                 client_->addr().ToString().c_str());
        client_->SendDeauthentication(reason_code::ReasonCode::kLeavingNetworkDeauth);
    }

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
            return service::SendEapolIndication(client_->device(), *eapol, hdr->addr2, hdr->addr3);
        }
        return ZX_OK;
    }

    // TODO(NET-463): Disallow data frames if RSNA is required but not established.

    const size_t eth_len = frame.body_len + sizeof(EthernetII);
    auto buffer = GetBuffer(eth_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto eth_packet = fbl::make_unique<Packet>(fbl::move(buffer), eth_len);
    // no need to clear the packet since every byte is overwritten
    eth_packet->set_peer(Packet::Peer::kEthernet);

    auto eth = eth_packet->mut_field<EthernetII>(0);
    eth->dest = hdr->addr3;
    eth->src = hdr->addr2;
    eth->ether_type = llc->protocol_id;
    std::memcpy(eth->payload, llc->payload, frame.body_len - sizeof(LlcHeader));

    auto status = client_->bss()->SendEthFrame(std::move(eth_packet));
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
        req_deauth_ = false;
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
        if (dozing_) {
            debugbss("[client] [%s] client is now dozing\n", client_->addr().ToString().c_str());
        } else {
            debugbss("[client] [%s] client woke up\n", client_->addr().ToString().c_str());
        }

        if (dozing_) {
            debugps("[client] [%s] client is now dozing\n", client_->addr().ToString().c_str());
        } else {
            debugps("[client] [%s] client woke up\n", client_->addr().ToString().c_str());

            // Send all buffered frames when client woke up.
            // TODO(hahnr): Once we implemented a smarter way of queuing packets, this code should
            // be revisited.
            while (client_->HasBufferedFrames()) {
                auto status = SendNextBu();
                if (status != ZX_OK) { return; }
            }
        }
    }
}

zx_status_t AssociatedState::HandleMlmeEapolReq(const wlan_mlme::EapolRequest& req) {
    size_t len = sizeof(DataFrameHeader) + sizeof(LlcHeader) + req.data->size();
    auto buffer = GetBuffer(len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::make_unique<Packet>(fbl::move(buffer), len);
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
    std::memcpy(llc->payload, req.data->data(), req.data->size());

    auto status = client_->bss()->SendDataFrame(fbl::move(packet));
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send EAPOL request packet: %d\n",
               client_->addr().ToString().c_str(), status);
        service::SendEapolResponse(client_->device(),
                                   wlan_mlme::EapolResultCodes::TRANSMISSION_FAILURE);
        return status;
    }

    service::SendEapolResponse(client_->device(), wlan_mlme::EapolResultCodes::SUCCESS);
    return status;
}

zx_status_t AssociatedState::SendNextBu() {
    ZX_DEBUG_ASSERT(client_->HasBufferedFrames());
    if (!client_->HasBufferedFrames()) { return ZX_ERR_BAD_STATE; }

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
    auto payload = packet->field<uint8_t>(sizeof(EthernetII));
    size_t payload_len = packet->len() - sizeof(EthernetII);
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
    debugps("[client] [%s] sent BU to client\n", client_->addr().ToString().c_str());
    return client_->bss()->SendDataFrame(fbl::move(data_packet));
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
    // Cleanly terminate the current state.
    state_->OnExit();
    state_.reset();

    debugbss("[client] [%s] destroyed\n", addr_.ToString().c_str());
}

void RemoteClient::MoveToState(fbl::unique_ptr<BaseState> to) {
    ZX_DEBUG_ASSERT(to != nullptr);
    auto from_id = state_ == nullptr ? StateId::kUninitialized : state_->id();
    if (to == nullptr) {
        errorf("attempt to transition to a nullptr from state: %hhu\n", from_id);
        return;
    }

    if (state_ != nullptr) { state_->OnExit(); }
    auto to_id = to->id();
    state_ = fbl::move(to);

    // Report state change to listener.
    if (listener_ != nullptr) { listener_->HandleClientStateChange(addr_, from_id, to_id); }

    // When the client's owner gets destroyed due to a state change, it will also destroy the state
    // which we were about to transition into. In that case, terminate.
    if (state_ != nullptr) {
        state_->OnEnter();
    }
}

void RemoteClient::HandleTimeout() {
    state_->HandleTimeout();
}

zx_status_t RemoteClient::HandleEthFrame(const ImmutableBaseFrame<EthernetII>& frame) {
    ForwardCurrentFrameTo(state_.get());
    return ZX_OK;
}

zx_status_t RemoteClient::HandleDataFrame(const DataFrameHeader& hdr) {
    ZX_DEBUG_ASSERT(hdr.addr2 == addr_);
    if (hdr.addr2 != addr_) { return ZX_ERR_STOP; }

    ForwardCurrentFrameTo(state_.get());
    return ZX_OK;
}

zx_status_t RemoteClient::HandleMgmtFrame(const MgmtFrameHeader& hdr) {
    ZX_DEBUG_ASSERT(hdr.addr2 == addr_);
    if (hdr.addr2 != addr_) { return ZX_ERR_STOP; }

    ForwardCurrentFrameTo(state_.get());
    return ZX_OK;
}

zx_status_t RemoteClient::HandlePsPollFrame(const ImmutableCtrlFrame<PsPollFrame>& frame,
                                            const wlan_rx_info_t& rxinfo) {
    ZX_DEBUG_ASSERT(frame.hdr->ta == addr_);
    if (frame.hdr->ta != addr_) { return ZX_ERR_STOP; }

    ForwardCurrentFrameTo(state_.get());
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
    hdr->addr1 = addr_;
    hdr->addr2 = bss_->bssid();
    hdr->addr3 = bss_->bssid();
    hdr->sc.set_seq(bss_->NextSeq(*hdr));

    auto auth = frame.body;
    auth->status_code = result;
    auth->auth_algorithm_number = AuthAlgorithm::kOpenSystem;
    // TODO(hahnr): Evolve this to support other authentication algorithms and track seq number.
    auth->auth_txn_seq_number = 2;

    auto status = bss_->SendMgmtFrame(fbl::move(packet));
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send auth response packet: %d\n", addr_.ToString().c_str(),
               status);
    }
    return status;
}

zx_status_t RemoteClient::SendAssociationResponse(aid_t aid, status_code::StatusCode result) {
    debugfn();
    debugbss("[client] [%s] sending Association Response\n", addr_.ToString().c_str());

    size_t body_payload_len = 256;
    fbl::unique_ptr<Packet> packet = nullptr;
    auto frame = BuildMgmtFrame<AssociationResponse>(&packet, body_payload_len);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto hdr = frame.hdr;
    const auto& bssid = bss_->bssid();
    hdr->addr1 = addr_;
    hdr->addr2 = bssid;
    hdr->addr3 = bssid;
    hdr->sc.set_seq(bss_->NextSeq(*hdr));
    FillTxInfo(&packet, *hdr);

    auto assoc = frame.body;
    assoc->status_code = result;
    assoc->aid = aid;
    assoc->cap.set_ess(1);
    assoc->cap.set_short_preamble(1);

    // Write elements.
    ElementWriter w(assoc->elements, body_payload_len);

    // Rates (in Mbps): 1 (basic), 2 (basic), 5.5 (basic), 6, 9, 11 (basic), 12, 18
    std::vector<uint8_t> rates = {0x82, 0x84, 0x8b, 0x0c, 0x12, 0x96, 0x18, 0x24};
    if (!w.write<SupportedRatesElement>(std::move(rates))) {
        errorf("[client] [%s] could not write supported rates\n", addr_.ToString().c_str());
        return ZX_ERR_IO;
    }

    // Rates (in Mbps): 24, 36, 48, 54
    std::vector<uint8_t> ext_rates = {0x30, 0x48, 0x60, 0x6c};
    if (!w.write<ExtendedSupportedRatesElement>(std::move(ext_rates))) {
        errorf("[client] [%s] could not write extended supported rates\n",
               addr_.ToString().c_str());
        return ZX_ERR_IO;
    }

    // Validate the request in debug mode.
    ZX_DEBUG_ASSERT(assoc->Validate(w.size()));

    size_t actual_len = hdr->len() + sizeof(AssociationResponse) + w.size();
    auto status = packet->set_len(actual_len);
    if (status != ZX_OK) {
        errorf("[client] [%s] could not set packet length to %zu: %d\n", addr_.ToString().c_str(),
               actual_len, status);
    }

    // TODO(NET-567): Write negotiated SupportedRates, ExtendedSupportedRates IEs

    if (bss_->IsHTReady()) {
        status = WriteHtCapabilities(&w);
        if (status != ZX_OK) { return status; }

        status = WriteHtOperation(&w);
        if (status != ZX_OK) { return status; }
    }

    body_payload_len = w.size();
    size_t frame_len = hdr->len() + sizeof(AssociationResponse) + body_payload_len;
    status = packet->set_len(frame_len);
    if (status != ZX_OK) {
        errorf("[client] [%s] could not set assocresp length to %zu: %d\n",
               addr_.ToString().c_str(), frame_len, status);
        return status;
    }

    status = bss_->SendMgmtFrame(fbl::move(packet));
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send auth response packet: %d\n", addr_.ToString().c_str(),
               status);
    }
    return status;
}

zx_status_t RemoteClient::SendDeauthentication(reason_code::ReasonCode reason_code) {
    debugfn();
    debugbss("[client] [%s] sending Deauthentication\n", addr_.ToString().c_str());

    fbl::unique_ptr<Packet> packet = nullptr;
    auto frame = BuildMgmtFrame<Deauthentication>(&packet);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto hdr = frame.hdr;
    hdr->addr1 = addr_;
    hdr->addr2 = bss_->bssid();
    hdr->addr3 = bss_->bssid();
    hdr->sc.set_seq(bss_->NextSeq(*hdr));

    auto deauth = frame.body;
    deauth->reason_code = reason_code;

    auto status = bss_->SendMgmtFrame(fbl::move(packet));
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send dauthentication packet: %d\n",
               addr_.ToString().c_str(), status);
    }
    return status;
}

zx_status_t RemoteClient::EnqueueEthernetFrame(const ImmutableBaseFrame<EthernetII>& frame) {
    // Drop oldest frame if queue reached its limit.
    if (ps_pkt_queue_.size() >= kMaxPowerSavingQueueSize) {
        ps_pkt_queue_.Dequeue();
        warnf("[client] [%s] dropping oldest unicast frame\n", addr().ToString().c_str());
    }

    debugps("[client] [%s] client is dozing; buffer outbound frame\n", addr().ToString().c_str());

    size_t hdr_len = sizeof(EthernetII);
    size_t frame_len = hdr_len + frame.body_len;
    auto buffer = GetBuffer(frame_len);
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
    const size_t buf_len = kDataFrameHdrLenMax + sizeof(LlcHeader) + frame.body_len;
    auto buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    *out_packet = fbl::make_unique<Packet>(std::move(buffer), buf_len);
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

    auto frame_len = hdr->len() + sizeof(LlcHeader) + frame.body_len;
    auto status = (*out_packet)->set_len(frame_len);
    if (status != ZX_OK) {
        errorf("[client] [%s] could not set data frame length to %zu: %d\n",
               addr_.ToString().c_str(), frame_len, status);
        return status;
    }

    (*out_packet)->CopyCtrlFrom(txinfo);

    return ZX_OK;
}

void RemoteClient::ReportBuChange(size_t bu_count) {
    if (listener_ != nullptr) { listener_->HandleClientBuChange(addr_, bu_count); }
}

zx_status_t RemoteClient::WriteHtCapabilities(ElementWriter* w) {
    HtCapabilities htc = bss_->BuildHtCapabilities();
    if (!w->write<HtCapabilities>(htc.ht_cap_info, htc.ampdu_params, htc.mcs_set, htc.ht_ext_cap,
                                  htc.txbf_cap, htc.asel_cap)) {
        errorf("[client] [%s] could not write HtCapabilities\n", addr_.ToString().c_str());
        return ZX_ERR_IO;
    }

    return ZX_OK;
}

zx_status_t RemoteClient::WriteHtOperation(ElementWriter* w) {
    auto chan = bss_->Chan();
    HtOperation hto = bss_->BuildHtOperation(chan);
    if (!w->write<HtOperation>(hto.primary_chan, hto.head, hto.tail, hto.mcs_set)) {
        errorf("[client] [%s] could not write HtOperation\n", addr_.ToString().c_str());
        return ZX_ERR_IO;
    }
    return ZX_OK;
}

#undef LOG_STATE_TRANSITION

}  // namespace wlan
