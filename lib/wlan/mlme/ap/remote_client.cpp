// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/ap/remote_client.h>

#include <wlan/mlme/debug.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

// BaseState implementation.

template <typename S, typename... Args> void BaseState::MoveToState(Args&&... args) {
    static_assert(fbl::is_base_of<BaseState, S>::value, "State class must implement BaseState");
    client_->MoveToState(fbl::make_unique<S>(client_, std::forward<Args>(args)...));
}

// Deauthenticating implementation.

DeauthenticatingState::DeauthenticatingState(RemoteClient* client) : BaseState(client) {}

void DeauthenticatingState::OnEnter() {
    debugfn();
    // TODO(hahnr): This is somewhat gross. Revisit once new frame processing
    // landed and the sate machine can make use of the new benefits.
    auto status = client_->ReportDeauthentication();
    if (status == ZX_OK) { MoveToState<DeauthenticatedState>(); }
}

// DeauthenticatedState implementation.

DeauthenticatedState::DeauthenticatedState(RemoteClient* client) : BaseState(client) {}

zx_status_t DeauthenticatedState::HandleAuthentication(const MgmtFrame<Authentication>& frame) {
    debugfn();
    ZX_DEBUG_ASSERT(frame.hdr()->addr2 == client_->addr());

    // Move into Authenticating state which responds to incoming Authentication
    // request.
    MoveToState<AuthenticatingState>(frame);
    return ZX_ERR_STOP;
}

// AuthenticatingState implementation.

AuthenticatingState::AuthenticatingState(RemoteClient* client,
                                         const MgmtFrame<Authentication>& frame)
    : BaseState(client) {
    ZX_DEBUG_ASSERT(frame.hdr()->addr2 == client_->addr());
    debugbss("[client] [%s] received Authentication request...\n",
             client_->addr().ToString().c_str());
    status_code_ = status_code::kRefusedReasonUnspecified;

    auto auth_alg = frame.body()->auth_algorithm_number;
    if (auth_alg != AuthAlgorithm::kOpenSystem) {
        errorf("[client] [%s] received auth attempt with unsupported algorithm: %u\n",
               client_->addr().ToString().c_str(), auth_alg);
        status_code_ = status_code::kUnsupportedAuthAlgorithm;
        return;
    }

    auto auth_txn_seq_no = frame.body()->auth_txn_seq_number;
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
        MoveToState<AuthenticatedState>();
    } else {
        MoveToState<DeauthenticatingState>();
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
    if (client_->IsDeadlineExceeded(auth_timeout_)) { MoveToState<DeauthenticatingState>(); }
}

zx_status_t AuthenticatedState::HandleAuthentication(const MgmtFrame<Authentication>& frame) {
    debugbss(
        "[client] [%s] received Authentication request while being "
        "authenticated\n",
        client_->addr().ToString().c_str());
    MoveToState<AuthenticatingState>(frame);
    return ZX_ERR_STOP;
}

zx_status_t AuthenticatedState::HandleDeauthentication(const MgmtFrame<Deauthentication>& frame) {
    debugbss("[client] [%s] received Deauthentication: %hu\n", client_->addr().ToString().c_str(),
             frame.body()->reason_code);
    MoveToState<DeauthenticatingState>();
    return ZX_ERR_STOP;
}

zx_status_t AuthenticatedState::HandleAssociationRequest(
    const MgmtFrame<AssociationRequest>& frame) {
    // Received request which we've been waiting for. Timer can get canceled.
    client_->CancelTimer();
    auth_timeout_ = zx::time();

    // Move into Associating state state which responds to incoming Association
    // requests.
    MoveToState<AssociatingState>(frame);
    return ZX_ERR_STOP;
}

// AssociatingState implementation.

AssociatingState::AssociatingState(RemoteClient* client, const MgmtFrame<AssociationRequest>& frame)
    : BaseState(client), status_code_(status_code::kRefusedReasonUnspecified), aid_(0) {
    debugfn();
    ZX_DEBUG_ASSERT(frame.hdr()->addr2 == client_->addr());
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
        MoveToState<AssociatedState>(aid_);
    } else {
        MoveToState<DeauthenticatingState>();
    }
}

// AssociatedState implementation.

AssociatedState::AssociatedState(RemoteClient* client, uint16_t aid)
    : BaseState(client), aid_(aid) {}

zx_status_t AssociatedState::HandleAuthentication(const MgmtFrame<Authentication>& frame) {
    debugbss("[client] [%s] received Authentication request while being associated\n",
             client_->addr().ToString().c_str());
    // Client believes it is not yet authenticated. Thus, there is no need to send
    // an explicit Deauthentication.
    req_deauth_ = false;

    MoveToState<AuthenticatingState>(frame);
    return ZX_ERR_STOP;
}

zx_status_t AssociatedState::HandleAssociationRequest(const MgmtFrame<AssociationRequest>& frame) {
    debugfn();
    ZX_DEBUG_ASSERT(frame.hdr()->addr2 == client_->addr());
    debugbss("[client] [%s] received Assocation Request while being associated\n",
             client_->addr().ToString().c_str());
    // Client believes it is not yet associated. Thus, there is no need to send an
    // explicit Deauthentication.
    req_deauth_ = false;

    MoveToState<AssociatingState>(frame);
    return ZX_ERR_STOP;
}

void AssociatedState::OnEnter() {
    debugbss("[client] [%s] acquired AID: %u\n", client_->addr().ToString().c_str(), aid_);

    inactive_timeout_ = client_->CreateTimerDeadline(kInactivityTimeoutTu);
    client_->StartTimer(inactive_timeout_);
    debugbss("[client] [%s] started inactivity timer\n", client_->addr().ToString().c_str());

    if (client_->bss()->IsRsn()) {
        debugbss("[client] [%s] requires RSNA\n", client_->addr().ToString().c_str());

        // TODO(NET-789): Block port only if RSN requires 802.1X authentication. For
        // now, only 802.1X authentications are supported.
        eapol_controlled_port_ = eapol::PortState::kBlocked;
    } else {
        eapol_controlled_port_ = eapol::PortState::kOpen;
    }

    // TODO(NET-833): Establish BlockAck session conditionally on the client capability
    // and the AP configurations
    client_->SendAddBaRequest();
}

zx_status_t AssociatedState::HandleEthFrame(const EthFrame& frame) {
    if (dozing_) {
        // Enqueue ethernet frame and postpone conversion to when the frame is sent
        // to the client.
        auto status = client_->EnqueueEthernetFrame(frame);
        if (status == ZX_ERR_NO_RESOURCES) {
            debugps("[client] [%s] reached PS buffering limit; dropping frame\n",
                    client_->addr().ToString().c_str());
        } else if (status != ZX_OK) {
            errorf("[client] couldn't enqueue ethernet frame: %d\n", status);
        }
        return status;
    }

    // If the client is awake and not in power saving mode, convert and send frame
    // immediately.
    fbl::unique_ptr<Packet> out_frame;
    auto status = client_->bss()->EthToDataFrame(frame, &out_frame);
    if (status != ZX_OK) {
        errorf("[client] couldn't convert ethernet frame: %d\n", status);
        return status;
    }
    return client_->bss()->SendDataFrame(fbl::move(out_frame));
}

zx_status_t AssociatedState::HandleDeauthentication(const MgmtFrame<Deauthentication>& frame) {
    debugbss("[client] [%s] received Deauthentication: %hu\n", client_->addr().ToString().c_str(),
             frame.body()->reason_code);
    req_deauth_ = false;
    MoveToState<DeauthenticatingState>();
    return ZX_ERR_STOP;
}

zx_status_t AssociatedState::HandleDisassociation(const MgmtFrame<Disassociation>& frame) {
    debugbss("[client] [%s] received Disassociation request: %u\n",
             client_->addr().ToString().c_str(), frame.body()->reason_code);
    MoveToState<AuthenticatedState>();
    return ZX_ERR_STOP;
}

zx_status_t AssociatedState::HandleCtrlFrame(const FrameControl& fc) {
    UpdatePowerSaveMode(fc);
    return ZX_OK;
}

zx_status_t AssociatedState::HandlePsPollFrame(const CtrlFrame<PsPollFrame>& frame) {
    debugbss("[client] [%s] client requested BU\n", client_->addr().ToString().c_str());

    if (client_->HasBufferedFrames()) { return SendNextBu(); }

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
        errorf(
            "[client] [%s] could not send null data frame as PS-POLL response: "
            "%d\n",
            client_->addr().ToString().c_str(), status);
        return status;
    }

    return ZX_OK;
}

void AssociatedState::OnExit() {
    client_->CancelTimer();
    inactive_timeout_ = zx::time();

    // Ensure Deauthentication is sent to the client if itself didn't send such
    // notification or such notification wasn't already sent due to inactivity of
    // the client. This Deauthentication is usually issued when the BSS stopped
    // and its associated clients need to get notified.
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

zx_status_t AssociatedState::HandleDataFrame(const DataFrame<LlcHeader>& frame) {
    if (frame.hdr()->fc.to_ds() == 0 || frame.hdr()->fc.from_ds() == 1) {
        warnf(
            "received unsupported data frame from %s with to_ds/from_ds "
            "combination: %u/%u\n",
            frame.hdr()->addr2.ToString().c_str(), frame.hdr()->fc.to_ds(),
            frame.hdr()->fc.from_ds());
        return ZX_OK;
    }

    auto hdr = frame.hdr();
    auto llc = frame.body();

    // Forward EAPOL frames to SME.
    size_t payload_len = frame.body_len() - sizeof(LlcHeader);
    if (be16toh(llc->protocol_id) == kEapolProtocolId) {
        if (payload_len < sizeof(EapolFrame)) {
            warnf("short EAPOL frame; len = %zu", payload_len);
            return ZX_OK;
        }

        auto eapol = reinterpret_cast<const EapolFrame*>(llc->payload);
        uint16_t actual_body_len = payload_len;
        uint16_t expected_body_len = be16toh(eapol->packet_body_length);
        if (actual_body_len != expected_body_len) {
            return service::SendEapolIndication(client_->device(), *eapol, hdr->addr2, hdr->addr3);
        }
        return ZX_OK;
    }

    // Block data frames if 802.1X authentication is required but didn't finish
    // yet.
    if (eapol_controlled_port_ != eapol::PortState::kOpen) { return ZX_OK; }

    const size_t eth_len = sizeof(EthernetII) + payload_len;
    auto buffer = GetBuffer(eth_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto eth_packet = fbl::make_unique<Packet>(fbl::move(buffer), eth_len);
    // no need to clear the packet since every byte is overwritten
    eth_packet->set_peer(Packet::Peer::kEthernet);

    auto eth = eth_packet->mut_field<EthernetII>(0);
    eth->dest = hdr->addr3;
    eth->src = hdr->addr2;
    eth->ether_type = llc->protocol_id;
    std::memcpy(eth->payload, llc->payload, payload_len - sizeof(LlcHeader));

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

        // The client timed-out, send Deauthentication. Ignore result, always leave
        // associated state.
        req_deauth_ = false;
        client_->SendDeauthentication(reason_code::ReasonCode::kReasonInactivity);
        debugbss(
            "[client] [%s] client inactive for %lu seconds; deauthenticating "
            "client\n",
            client_->addr().ToString().c_str(), kInactivityTimeoutTu / 1000);
        MoveToState<DeauthenticatedState>();
    }
}

void AssociatedState::UpdatePowerSaveMode(const FrameControl& fc) {
    if (fc.pwr_mgmt() != dozing_) {
        dozing_ = fc.pwr_mgmt();

        if (dozing_) {
            debugps("[client] [%s] client is now dozing\n", client_->addr().ToString().c_str());
        } else {
            debugps("[client] [%s] client woke up\n", client_->addr().ToString().c_str());

            // Send all buffered frames when client woke up.
            // TODO(hahnr): Once we implemented a smarter way of queuing packets, this
            // code should be revisited.
            while (client_->HasBufferedFrames()) {
                auto status = SendNextBu();
                if (status != ZX_OK) { return; }
            }
        }
    }
}

zx_status_t AssociatedState::HandleMlmeEapolReq(const MlmeMsg<wlan_mlme::EapolRequest>& req) {
    size_t len = sizeof(DataFrameHeader) + sizeof(LlcHeader) + req.body()->data->size();
    auto buffer = GetBuffer(len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::make_unique<Packet>(fbl::move(buffer), len);
    packet->clear();
    packet->set_peer(Packet::Peer::kWlan);

    auto hdr = packet->mut_field<DataFrameHeader>(0);
    hdr->fc.set_type(FrameType::kData);
    hdr->fc.set_from_ds(1);
    hdr->addr1.Set(req.body()->dst_addr.data());
    hdr->addr2 = client_->bss()->bssid();
    hdr->addr3.Set(req.body()->src_addr.data());
    hdr->sc.set_seq(client_->bss()->NextSeq(*hdr));

    auto llc = packet->mut_field<LlcHeader>(sizeof(DataFrameHeader));
    llc->dsap = kLlcSnapExtension;
    llc->ssap = kLlcSnapExtension;
    llc->control = kLlcUnnumberedInformation;
    std::memcpy(llc->oui, kLlcOui, sizeof(llc->oui));
    llc->protocol_id = htobe16(kEapolProtocolId);
    std::memcpy(llc->payload, req.body()->data->data(), req.body()->data->size());

    auto status = client_->bss()->SendDataFrame(fbl::move(packet));
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send EAPOL request packet: %d\n",
               client_->addr().ToString().c_str(), status);
        service::SendEapolConfirm(client_->device(),
                                  wlan_mlme::EapolResultCodes::TRANSMISSION_FAILURE);
        return status;
    }

    service::SendEapolConfirm(client_->device(), wlan_mlme::EapolResultCodes::SUCCESS);
    return status;
}

zx_status_t AssociatedState::HandleMlmeSetKeysReq(const MlmeMsg<wlan_mlme::SetKeysRequest>& req) {
    debugfn();

    for (auto& keyDesc : *req.body()->keylist) {
        if (keyDesc.key.is_null()) { return ZX_ERR_NOT_SUPPORTED; }

        switch (keyDesc.key_type) {
        case wlan_mlme::KeyType::PAIRWISE:
            // Once a pairwise key was exchange, RSNA was established.
            // TODO(NET-790): This is a pretty simplified assumption and an RSNA
            // should only be established once all required keys by the RSNE were
            // exchanged.
            eapol_controlled_port_ = eapol::PortState::kOpen;
        default:
            break;
        }

        // TODO(hahnr): Configure Key in hardware.
    }

    return ZX_OK;
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

    // Treat Packet as Ethernet frame and convert Ethernet to Data frame.
    EthFrame eth_frame(fbl::move(packet));
    fbl::unique_ptr<Packet> data_packet;
    status = client_->bss()->EthToDataFrame(eth_frame, &data_packet);
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

zx_status_t AssociatedState::HandleActionFrame(const MgmtFrame<ActionFrame>& frame) {
    debugfn();

    // TODO(porce): Handle AddBaResponses and keep the result of negotiation.

    // TODO(hahnr): We need to use a FrameView until frames are moved rather than passed by const
    // reference.
    auto mgmt_frame = frame.View();

    // TODO(hahnr): Frame::NextFrame should do the validation and return an empty frame on error.
    // Remove the validation here once we merged the new validation framework for frames.
    if (mgmt_frame.body()->category != ActionFrameBlockAck::ActionCategory()) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    auto action_frame = mgmt_frame.NextFrame();
    if (action_frame.body_len() < sizeof(ActionFrameBlockAck)) { return ZX_ERR_NOT_SUPPORTED; }

    auto ba_frame = action_frame.NextFrame<ActionFrameBlockAck>();
    if (ba_frame.hdr()->action == AddBaResponseFrame::BlockAckAction()) {
        if (ba_frame.body_len() < sizeof(AddBaResponseFrame)) { return ZX_ERR_NOT_SUPPORTED; }

        auto addbarresp_frame = ba_frame.NextFrame<AddBaResponseFrame>();
        auto addbarresp = addbarresp_frame.hdr();
        finspect("Inbound ADDBA Resp frame: len %zu\n", addbarresp_frame.len());
        finspect("  addba resp: %s\n", debug::Describe(*addbarresp).c_str());
        return ZX_OK;
    } else if (ba_frame.hdr()->action == AddBaRequestFrame::BlockAckAction()) {
        if (ba_frame.body_len() < sizeof(AddBaRequestFrame)) { return ZX_ERR_NOT_SUPPORTED; }

        auto addbarreq_frame = ba_frame.NextFrame<AddBaRequestFrame>();
        auto addbarreq = addbarreq_frame.hdr();
        finspect("Inbound ADDBA Req frame: len %zu\n", addbarreq_frame.len());
        finspect("  addba req: %s\n", debug::Describe(*addbarreq).c_str());
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ZX_ERR_NOT_SUPPORTED;
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
}

RemoteClient::~RemoteClient() {
    // Cleanly terminate the current state.
    state_->OnExit();
    state_.reset();

    debugbss("[client] [%s] destroyed\n", addr_.ToString().c_str());
}

void RemoteClient::MoveToState(fbl::unique_ptr<BaseState> to) {
    ZX_DEBUG_ASSERT(to != nullptr);
    auto from_name = state_ == nullptr ? "()" : state_->name();
    if (to == nullptr) {
        errorf("attempt to transition to a nullptr from state: %s\n", from_name);
        return;
    }

    if (state_ != nullptr) { state_->OnExit(); }

    debugbss("[client] [%s] %s -> %s\n", addr().ToString().c_str(), from_name, to->name());
    state_ = fbl::move(to);

    state_->OnEnter();
}

void RemoteClient::HandleTimeout() {
    state_->HandleTimeout();
}

zx_status_t RemoteClient::HandleEthFrame(const EthFrame& frame) {
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

zx_status_t RemoteClient::HandlePsPollFrame(const CtrlFrame<PsPollFrame>& frame) {
    ZX_DEBUG_ASSERT(frame.body()->ta == addr_);
    if (frame.body()->ta != addr_) { return ZX_ERR_STOP; }

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

zx::time RemoteClient::CreateTimerDeadline(wlan_tu_t tus) {
    return timer_->Now() + WLAN_TU(tus);
}

bool RemoteClient::IsDeadlineExceeded(zx::time deadline) {
    return deadline > zx::time() && timer_->Now() >= deadline;
}

zx_status_t RemoteClient::SendAuthentication(status_code::StatusCode result) {
    debugfn();
    debugbss("[client] [%s] sending Authentication response\n", addr_.ToString().c_str());

    MgmtFrame<Authentication> frame;
    auto status = BuildMgmtFrame(&frame);
    if (status != ZX_OK) { return status; }

    frame.FillTxInfo();

    auto hdr = frame.hdr();
    hdr->addr1 = addr_;
    hdr->addr2 = bss_->bssid();
    hdr->addr3 = bss_->bssid();
    hdr->sc.set_seq(bss_->NextSeq(*hdr));

    auto auth = frame.body();
    auth->status_code = result;
    auth->auth_algorithm_number = AuthAlgorithm::kOpenSystem;
    // TODO(hahnr): Evolve this to support other authentication algorithms and
    // track seq number.
    auth->auth_txn_seq_number = 2;

    status = bss_->SendMgmtFrame(frame.Take());
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
    MgmtFrame<AssociationResponse> frame;
    auto status = BuildMgmtFrame(&frame, body_payload_len);
    if (status != ZX_OK) { return status; }

    auto hdr = frame.hdr();
    const auto& bssid = bss_->bssid();
    hdr->addr1 = addr_;
    hdr->addr2 = bssid;
    hdr->addr3 = bssid;
    hdr->sc.set_seq(bss_->NextSeq(*hdr));
    frame.FillTxInfo();

    auto assoc = frame.body();
    assoc->status_code = result;
    assoc->aid = aid;
    assoc->cap.set_ess(1);
    assoc->cap.set_short_preamble(1);

    // Write elements.
    ElementWriter w(assoc->elements, body_payload_len);

    // Rates (in Mbps): 6(B), 9, 12(B), 18, 24(B), 36, 48, 54
    std::vector<uint8_t> rates = {0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c};
    if (!w.write<SupportedRatesElement>(std::move(rates))) {
        errorf("[client] [%s] could not write supported rates\n", addr_.ToString().c_str());
        return ZX_ERR_IO;
    }

    // TODO(NET-567): Write negotiated SupportedRates, ExtendedSupportedRates IEs

    if (bss_->IsHTReady()) {
        auto status = WriteHtCapabilities(&w);
        if (status != ZX_OK) { return status; }

        status = WriteHtOperation(&w);
        if (status != ZX_OK) { return status; }
    }

    // Validate the request in debug mode.
    ZX_DEBUG_ASSERT(assoc->Validate(w.size()));

    size_t body_len = sizeof(AssociationResponse) + w.size();
    status = frame.set_body_len(body_len);
    if (status != ZX_OK) {
        errorf("[client] [%s] could not set assocresp length to %zu: %d\n",
               addr_.ToString().c_str(), body_len, status);
        return status;
    }

    status = bss_->SendMgmtFrame(frame.Take());
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send auth response packet: %d\n", addr_.ToString().c_str(),
               status);
    }
    return status;
}

zx_status_t RemoteClient::SendDeauthentication(reason_code::ReasonCode reason_code) {
    debugfn();
    debugbss("[client] [%s] sending Deauthentication\n", addr_.ToString().c_str());

    MgmtFrame<Deauthentication> frame;
    auto status = BuildMgmtFrame(&frame);
    if (status != ZX_OK) { return status; }

    auto hdr = frame.hdr();
    hdr->addr1 = addr_;
    hdr->addr2 = bss_->bssid();
    hdr->addr3 = bss_->bssid();
    hdr->sc.set_seq(bss_->NextSeq(*hdr));

    auto deauth = frame.body();
    deauth->reason_code = static_cast<uint16_t>(reason_code);

    status = bss_->SendMgmtFrame(frame.Take());
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send dauthentication packet: %d\n",
               addr_.ToString().c_str(), status);
    }
    return status;
}

zx_status_t RemoteClient::EnqueueEthernetFrame(const EthFrame& frame) {
    // Drop oldest frame if queue reached its limit.
    if (bu_queue_.size() >= kMaxPowerSavingQueueSize) {
        bu_queue_.Dequeue();
        warnf("[client] [%s] dropping oldest unicast frame\n", addr().ToString().c_str());
    }

    debugps("[client] [%s] client is dozing; buffer outbound frame\n", addr().ToString().c_str());

    size_t eth_len = frame.len();
    auto buffer = GetBuffer(eth_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    // Copy ethernet frame into buffer acquired from the BSS.
    auto packet = fbl::make_unique<Packet>(std::move(buffer), eth_len);
    memcpy(packet->mut_data(), frame.hdr(), eth_len);

    bu_queue_.Enqueue(fbl::move(packet));
    ReportBuChange(bu_queue_.size());

    return ZX_OK;
}

zx_status_t RemoteClient::DequeueEthernetFrame(fbl::unique_ptr<Packet>* out_packet) {
    ZX_DEBUG_ASSERT(bu_queue_.size() > 0);
    if (bu_queue_.size() == 0) { return ZX_ERR_NO_RESOURCES; }

    *out_packet = bu_queue_.Dequeue();
    ReportBuChange(bu_queue_.size());

    return ZX_OK;
}

bool RemoteClient::HasBufferedFrames() const {
    return bu_queue_.size() > 0;
}

void RemoteClient::ReportBuChange(size_t bu_count) {
    if (listener_ != nullptr) { listener_->HandleClientBuChange(addr_, bu_count); }
}

zx_status_t RemoteClient::ReportDeauthentication() {
    if (listener_ != nullptr) { return listener_->HandleClientDeauth(addr_); }
    return ZX_OK;
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

uint8_t RemoteClient::GetTid() {
    // TODO(NET-599): Implement QoS policy engine.
    return 0;
}

zx_status_t RemoteClient::SendAddBaRequest() {
    debugfn();
    if (!bss_->IsHTReady()) { return ZX_OK; }

    debugbss("[client] [%s] sending AddBaRequest\n", addr_.ToString().c_str());

    MgmtFrame<ActionFrame> tx_frame;
    size_t payload_len = sizeof(ActionFrameBlockAck) + sizeof(AddBaRequestFrame);
    auto status = BuildMgmtFrame(&tx_frame, payload_len);
    if (status != ZX_OK) { return status; }

    auto hdr = tx_frame.hdr();
    hdr->addr1 = addr_;
    hdr->addr2 = bss_->bssid();
    hdr->addr3 = bss_->bssid();
    hdr->sc.set_seq(bss_->NextSeq(*hdr));
    tx_frame.FillTxInfo();

    auto action_hdr = tx_frame.body();
    action_hdr->category = ActionFrameBlockAck::ActionCategory();

    auto ba_frame = tx_frame.NextFrame<ActionFrameBlockAck>();
    ba_frame.hdr()->action = AddBaRequestFrame::BlockAckAction();

    auto addbareq_frame = ba_frame.NextFrame<AddBaRequestFrame>();
    auto addbareq = addbareq_frame.hdr();
    // It appears there is no particular rule to choose the value for
    // dialog_token. See IEEE Std 802.11-2016, 9.6.5.2.
    addbareq->dialog_token = 0x01;
    addbareq->params.set_amsdu(0);
    addbareq->params.set_policy(BlockAckParameters::BlockAckPolicy::kImmediate);
    addbareq->params.set_tid(GetTid());  // TODO(NET-599): Communicate this with lower MAC.
    // TODO(porce): Fix the discrepancy of this value from the Ralink's TXWI ba_win_size setting
    addbareq->params.set_buffer_size(64);
    addbareq->timeout = 0;               // Disables the timeout
    addbareq->seq_ctrl.set_fragment(0);  // TODO(NET-599): Send this down to the lower MAC
    addbareq->seq_ctrl.set_starting_seq(1);

    finspect("Outbound ADDBA Req frame: len %zu\n", addbareq_frame.len());
    finspect("  addba req: %s\n", debug::Describe(*addbareq).c_str());

    status = bss_->SendMgmtFrame(addbareq_frame.Take());
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send AddbaRequest: %d\n", addr_.ToString().c_str(), status);
    }

    return ZX_OK;
}

zx_status_t RemoteClient::SendAddBaResponse(const AddBaRequestFrame& req) {
    MgmtFrame<ActionFrame> tx_frame;
    size_t payload_len = sizeof(ActionFrameBlockAck) + sizeof(AddBaRequestFrame);
    auto status = BuildMgmtFrame(&tx_frame, payload_len);
    if (status != ZX_OK) { return status; }

    auto hdr = tx_frame.hdr();
    hdr->addr1 = addr_;
    hdr->addr2 = bss_->bssid();
    hdr->addr3 = bss_->bssid();
    hdr->sc.set_seq(bss_->NextSeq(*hdr));
    tx_frame.FillTxInfo();

    auto action_frame = tx_frame.body();
    action_frame->category = ActionFrameBlockAck::ActionCategory();

    auto ba_frame = tx_frame.NextFrame<ActionFrameBlockAck>();
    ba_frame.hdr()->action = AddBaResponseFrame::BlockAckAction();

    auto addbaresp_frame = ba_frame.NextFrame<AddBaResponseFrame>();
    auto addbaresp = addbaresp_frame.hdr();
    addbaresp->dialog_token = req.dialog_token;

    // TODO(porce): Implement DelBa as a response to AddBar for decline

    addbaresp->status_code = status_code::kSuccess;

    // TODO(NET-567): Use the outcome of the association negotiation
    addbaresp->params.set_amsdu(0);
    addbaresp->params.set_policy(BlockAckParameters::kImmediate);
    addbaresp->params.set_tid(req.params.tid());

    // TODO(NET-565, NET-567): Use the chipset's buffer_size
    auto buffer_size_ap = req.params.buffer_size();
    constexpr size_t buffer_size_ralink = 64;
    auto buffer_size = (buffer_size_ap <= buffer_size_ralink) ? buffer_size_ap : buffer_size_ralink;
    addbaresp->params.set_buffer_size(buffer_size);

    addbaresp->timeout = req.timeout;

    finspect("Outbound ADDBA Resp frame: len %zu\n", addbaresp_frame.len());
    finspect("Outbound Mgmt Frame(ADDBA Resp): %s\n", debug::Describe(*addbaresp).c_str());

    status = bss_->SendMgmtFrame(addbaresp_frame.Take());
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send AddBaResponse: %d\n", addr_.ToString().c_str(),
               status);
        return status;
    }

    return ZX_OK;
}

}  // namespace wlan
