// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/ap/remote_client.h>

#include <wlan/common/buffer_writer.h>
#include <wlan/common/element_splitter.h>
#include <wlan/common/parse_element.h>
#include <wlan/common/write_element.h>
#include <wlan/mlme/convert.h>
#include <wlan/mlme/debug.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/rates_elements.h>
#include <wlan/mlme/service.h>
#include <zircon/status.h>

namespace wlan {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

// BaseState implementation.

template <typename S, typename... Args> void BaseState::MoveToState(Args&&... args) {
    static_assert(fbl::is_base_of<BaseState, S>::value, "State class must implement BaseState");
    client_->MoveToState(fbl::make_unique<S>(client_, std::forward<Args>(args)...));
}

// Deauthenticating implementation.

DeauthenticatingState::DeauthenticatingState(RemoteClient* client,
                                             wlan_mlme::ReasonCode reason_code,
                                             bool send_deauth_frame)
    : BaseState(client), reason_code_(reason_code), send_deauth_frame_(send_deauth_frame) {}

void DeauthenticatingState::OnEnter() {
    debugfn();
    service::SendDeauthIndication(client_->device(), client_->addr(), reason_code_);
    if (send_deauth_frame_) { client_->SendDeauthentication(reason_code_); }
    MoveToState<DeauthenticatedState>(DeauthenticatedState::MoveReason::EXPLICIT_DEAUTH);
}

// DeauthenticatedState implementation.

DeauthenticatedState::DeauthenticatedState(RemoteClient* client,
                                           DeauthenticatedState::MoveReason move_reason)
    : BaseState(client), move_reason_(move_reason) {}

void DeauthenticatedState::OnEnter() {
    switch (move_reason_) {
    case DeauthenticatedState::MoveReason::INIT:
    case DeauthenticatedState::MoveReason::REAUTH:
        break;  // nothing to do
    case DeauthenticatedState::MoveReason::EXPLICIT_DEAUTH:
        client_->ReportDeauthentication();
        break;
    case DeauthenticatedState::MoveReason::FAILED_AUTH:
        client_->ReportFailedAuth();
        break;
    }
}

void DeauthenticatedState::HandleAnyMgmtFrame(MgmtFrame<>&& frame) {
    if (auto auth_frame = frame.View().CheckBodyType<Authentication>().CheckLength()) {
        ZX_DEBUG_ASSERT(frame.hdr()->addr2 == client_->addr());
        debugbss("[client] [%s] received Authentication request...\n",
                 client_->addr().ToString().c_str());

        auto auth_alg = auth_frame.body()->auth_algorithm_number;
        if (auth_alg != AuthAlgorithm::kOpenSystem) {
            errorf("[client] [%s] received auth attempt with unsupported algorithm: %u\n",
                   client_->addr().ToString().c_str(), auth_alg);
            FailAuthentication(WLAN_STATUS_CODE_UNSUPPORTED_AUTH_ALGORITHM);
            return;
        }

        auto auth_txn_seq_no = auth_frame.body()->auth_txn_seq_number;
        if (auth_txn_seq_no != 1) {
            errorf("[client] [%s] received auth attempt with invalid tx seq no: %u\n",
                   client_->addr().ToString().c_str(), auth_txn_seq_no);
            FailAuthentication(WLAN_STATUS_CODE_REFUSED);
            return;
        }

        service::SendAuthIndication(client_->device(), client_->addr(),
                                    wlan_mlme::AuthenticationTypes::OPEN_SYSTEM);
        MoveToState<AuthenticatingState>();
    }
}

void DeauthenticatedState::FailAuthentication(const wlan_status_code st_code) {
    client_->SendAuthentication(st_code);
    client_->ReportFailedAuth();
}

// AuthenticatingState implementation.

AuthenticatingState::AuthenticatingState(RemoteClient* client) : BaseState(client) {}

void AuthenticatingState::OnEnter() {
    client_->ScheduleTimer(kAuthenticatingTimeoutTu, &auth_timeout_);
}

void AuthenticatingState::OnExit() {
    client_->CancelTimer(auth_timeout_);
}

void AuthenticatingState::HandleTimeout(TimeoutId id) {
    if (auth_timeout_ == id) {
        warnf("[client] [%s] timed out authenticating\n", client_->addr().ToString().c_str());
        MoveToState<DeauthenticatedState>(DeauthenticatedState::MoveReason::FAILED_AUTH);
    }
}

zx_status_t AuthenticatingState::HandleMlmeMsg(const BaseMlmeMsg& msg) {
    if (auto auth_resp = msg.As<wlan_mlme::AuthenticateResponse>()) {
        ZX_DEBUG_ASSERT(client_->addr() ==
                        common::MacAddr(auth_resp->body()->peer_sta_address.data()));
        // Received request which we've been waiting for. Timer can get canceled.
        client_->CancelTimer(auth_timeout_);

        wlan_status_code st_code = ToStatusCode(auth_resp->body()->result_code);
        return FinalizeAuthenticationAttempt(st_code);
    } else {
        warnf("[client] [%s] unexpected MLME msg type in authenticating state; ordinal: %u\n",
              client_->addr().ToString().c_str(), msg.ordinal());
        return ZX_ERR_INVALID_ARGS;
    }
}

zx_status_t AuthenticatingState::FinalizeAuthenticationAttempt(const wlan_status_code st_code) {
    bool auth_success = st_code == WLAN_STATUS_CODE_SUCCESS;
    auto status = client_->SendAuthentication(st_code);
    if (auth_success && status == ZX_OK) {
        MoveToState<AuthenticatedState>();
    } else {
        MoveToState<DeauthenticatedState>(DeauthenticatedState::MoveReason::FAILED_AUTH);
    }
    return status;
}

// AuthenticatedState implementation.

AuthenticatedState::AuthenticatedState(RemoteClient* client) : BaseState(client) {}

void AuthenticatedState::OnEnter() {
    // Start timeout and wait for Association requests.
    client_->ScheduleTimer(kAuthenticationTimeoutTu, &auth_timeout_);
}

void AuthenticatedState::OnExit() {
    client_->CancelTimer(auth_timeout_);
}

void AuthenticatedState::HandleTimeout(TimeoutId id) {
    if (auth_timeout_ == id) {
        bool send_deauth_frame = true;
        MoveToState<DeauthenticatingState>(wlan_mlme::ReasonCode::REASON_INACTIVITY,
                                           send_deauth_frame);
    }
}

void AuthenticatedState::HandleAnyMgmtFrame(MgmtFrame<>&& frame) {
    if (auto auth = frame.View().CheckBodyType<Authentication>().CheckLength()) {
        HandleAuthentication(auth.IntoOwned(frame.Take()));
    } else if (auto assoc_req = frame.View().CheckBodyType<AssociationRequest>().CheckLength()) {
        HandleAssociationRequest(assoc_req.IntoOwned(frame.Take()));
    } else if (auto deauth = frame.View().CheckBodyType<Deauthentication>().CheckLength()) {
        HandleDeauthentication(deauth.IntoOwned(frame.Take()));
    }
}

void AuthenticatedState::HandleAuthentication(MgmtFrame<Authentication>&& frame) {
    debugbss(
        "[client] [%s] received Authentication request while being "
        "authenticated\n",
        client_->addr().ToString().c_str());
    // After the `MovedToState` call, the memory location for variable `client_` is no longer valid
    // because current state is destroyed. Thus, save pointer on the stack first.
    auto saved_client = client_;
    MoveToState<DeauthenticatedState>(DeauthenticatedState::MoveReason::REAUTH);
    saved_client->HandleAnyMgmtFrame(MgmtFrame<>(frame.Take()));
}

void AuthenticatedState::HandleDeauthentication(MgmtFrame<Deauthentication>&& frame) {
    debugbss("[client] [%s] received Deauthentication: %hu\n", client_->addr().ToString().c_str(),
             frame.body()->reason_code);
    bool send_deauth_frame = false;
    MoveToState<DeauthenticatingState>(
        static_cast<wlan_mlme::ReasonCode>(frame.body()->reason_code), send_deauth_frame);
}

void AuthenticatedState::HandleAssociationRequest(MgmtFrame<AssociationRequest>&& frame) {
    debugfn();
    ZX_DEBUG_ASSERT(frame.hdr()->addr2 == client_->addr());
    debugbss("[client] [%s] received Assocation Request\n", client_->addr().ToString().c_str());

    auto assoc_req_frame = frame.View().NextFrame();
    Span<const uint8_t> ies = assoc_req_frame.body_data();

    std::optional<Span<const uint8_t>> ssid;
    std::optional<Span<const uint8_t>> rsn_body;
    for (auto [id, raw_body] : common::ElementSplitter(ies)) {
        switch (id) {
        case element_id::kSsid:
            ssid = common::ParseSsid(raw_body);
            break;
        case element_id::kRsn:
            rsn_body = {raw_body};
            break;
        default:
            break;
        }
    }

    if (!ssid) { return; }

    // Received a valid association request. We can cancel the timer now.
    client_->CancelTimer(auth_timeout_);
    zx_status_t status = service::SendAssocIndication(
        client_->device(), client_->addr(), frame.body()->listen_interval, *ssid, rsn_body);
    if (status != ZX_OK) {
        errorf("Failed to send AssocIndication service message: %s\n",
               zx_status_get_string(status));
    }
    MoveToState<AssociatingState>();
}

// AssociatingState implementation.

AssociatingState::AssociatingState(RemoteClient* client) : BaseState(client) {}

void AssociatingState::OnEnter() {
    client_->ScheduleTimer(kAssociatingTimeoutTu, &assoc_timeout_);
}

void AssociatingState::OnExit() {
    client_->CancelTimer(assoc_timeout_);
}

void AssociatingState::HandleTimeout(TimeoutId id) {
    if (assoc_timeout_ == id) {
        warnf("[client] [%s] timed out associating\n", client_->addr().ToString().c_str());
        MoveToState<AuthenticatedState>();
    }
}

zx_status_t AssociatingState::HandleMlmeMsg(const BaseMlmeMsg& msg) {
    if (auto assoc_resp = msg.As<wlan_mlme::AssociateResponse>()) {
        ZX_DEBUG_ASSERT(client_->addr() ==
                        common::MacAddr(assoc_resp->body()->peer_sta_address.data()));
        // Received request which we've been waiting for. Timer can get canceled.
        client_->CancelTimer(assoc_timeout_);

        std::optional<uint16_t> aid = {};
        wlan_status_code st_code = ToStatusCode(assoc_resp->body()->result_code);
        if (st_code == WLAN_STATUS_CODE_SUCCESS) { aid = {assoc_resp->body()->association_id}; }
        return FinalizeAssociationAttempt(aid, st_code);
    } else {
        warnf("[client] [%s] unexpected MLME msg type in associating state; ordinal: %u\n",
              client_->addr().ToString().c_str(), msg.ordinal());
        return ZX_ERR_INVALID_ARGS;
    }
}

zx_status_t AssociatingState::FinalizeAssociationAttempt(std::optional<uint16_t> aid,
                                                         wlan_status_code st_code) {
    bool assoc_success = aid.has_value() && st_code == WLAN_STATUS_CODE_SUCCESS;
    auto status = client_->SendAssociationResponse(aid.value_or(0), st_code);
    if (assoc_success && status == ZX_OK) {
        MoveToState<AssociatedState>(aid.value());
    } else {
        service::SendDisassociateIndication(client_->device(), client_->addr(),
                                            WLAN_REASON_CODE_UNSPECIFIED_REASON);
        MoveToState<AuthenticatedState>();
    }
    return status;
}

// AssociatedState implementation.

AssociatedState::AssociatedState(RemoteClient* client, uint16_t aid)
    : BaseState(client), aid_(aid) {}

void AssociatedState::HandleAnyDataFrame(DataFrame<>&& frame) {
    UpdatePowerSaveMode(frame.hdr()->fc);

    // TODO(hahnr): Handle A-MSDUs (mandatory for 802.11n)

    if (auto llc_frame = frame.View().CheckBodyType<LlcHeader>().CheckLength()) {
        HandleDataLlcFrame(llc_frame.IntoOwned(frame.Take()));
    }
}

void AssociatedState::HandleAnyMgmtFrame(MgmtFrame<>&& frame) {
    UpdatePowerSaveMode(frame.hdr()->fc);

    if (auto auth = frame.View().CheckBodyType<Authentication>().CheckLength()) {
        HandleAuthentication(auth.IntoOwned(frame.Take()));
    } else if (auto assoc_req = frame.View().CheckBodyType<AssociationRequest>().CheckLength()) {
        HandleAssociationRequest(assoc_req.IntoOwned(frame.Take()));
    } else if (auto deauth = frame.View().CheckBodyType<Deauthentication>().CheckLength()) {
        HandleDeauthentication(deauth.IntoOwned(frame.Take()));
    } else if (auto disassoc = frame.View().CheckBodyType<Disassociation>().CheckLength()) {
        HandleDisassociation(disassoc.IntoOwned(frame.Take()));
    } else if (auto action = frame.View().CheckBodyType<ActionFrame>().CheckLength()) {
        HandleActionFrame(action.IntoOwned(frame.Take()));
    }
}

void AssociatedState::HandleAnyCtrlFrame(CtrlFrame<>&& frame) {
    UpdatePowerSaveMode(frame.hdr()->fc);

    if (auto pspoll = frame.View().CheckBodyType<PsPollFrame>().CheckLength()) {
        if (aid_ != pspoll.body()->aid) { return; }
        HandlePsPollFrame(pspoll.IntoOwned(frame.Take()));
    }
}

void AssociatedState::HandleAuthentication(MgmtFrame<Authentication>&& frame) {
    debugbss("[client] [%s] received Authentication request while being associated\n",
             client_->addr().ToString().c_str());
    // After the `MovedToState` call, the memory location for variable `client_` is no longer valid
    // because current state is destroyed. Thus, save pointer on the stack first.
    auto saved_client = client_;
    MoveToState<DeauthenticatedState>(DeauthenticatedState::MoveReason::REAUTH);
    saved_client->HandleAnyMgmtFrame(MgmtFrame<>(frame.Take()));
}

void AssociatedState::HandleAssociationRequest(MgmtFrame<AssociationRequest>&& frame) {
    debugfn();
    ZX_DEBUG_ASSERT(frame.hdr()->addr2 == client_->addr());
    debugbss("[client] [%s] received Assocation Request while being associated\n",
             client_->addr().ToString().c_str());
    // Client believes it is not yet associated. Move it back to authenticated state and then have
    // it process the frame.
    MoveToState<AuthenticatedState>();
    client_->HandleAnyMgmtFrame(MgmtFrame<>(frame.Take()));
}

void AssociatedState::OnEnter() {
    debugbss("[client] [%s] acquired AID: %u\n", client_->addr().ToString().c_str(), aid_);

    client_->ScheduleTimer(kInactivityTimeoutTu, &inactive_timeout_);
    debugbss("[client] [%s] started inactivity timer\n", client_->addr().ToString().c_str());

    if (client_->bss()->IsRsn()) {
        debugbss("[client] [%s] requires RSNA\n", client_->addr().ToString().c_str());

        // TODO(NET-789): Block port only if RSN requires 802.1X authentication. For
        // now, only 802.1X authentications are supported.
        eapol_controlled_port_ = eapol::PortState::kBlocked;
    } else {
        eapol_controlled_port_ = eapol::PortState::kOpen;
    }

    wlan_assoc_ctx_t assoc = client_->BuildAssocContext(aid_);
    client_->device()->ConfigureAssoc(&assoc);

    // TODO(NET-833): Establish BlockAck session conditionally on the client capability
    // and the AP configurations
    client_->SendAddBaRequest();
}

void AssociatedState::HandleEthFrame(EthFrame&& eth_frame) {
    if (dozing_) {
        // Enqueue ethernet frame and postpone conversion to when the frame is sent
        // to the client.
        auto status = EnqueueEthernetFrame(std::move(eth_frame));
        if (status == ZX_ERR_NO_RESOURCES) {
            debugps("[client] [%s] reached PS buffering limit; dropping frame\n",
                    client_->addr().ToString().c_str());
        } else if (status != ZX_OK) {
            errorf("[client] couldn't enqueue ethernet frame: %d\n", status);
        }
        return;
    }

    // If the client is awake and not in power saving mode, convert and send frame
    // immediately.
    auto data_frame = EthToDataFrame(eth_frame);
    if (!data_frame) {
        errorf("[client] couldn't convert ethernet frame\n");
        return;
    }
    client_->bss()->SendDataFrame(DataFrame<>(data_frame->Take()),
                                  client_->is_qos_ready() ? WLAN_TX_INFO_FLAGS_QOS : 0);
}

void AssociatedState::HandleDeauthentication(MgmtFrame<Deauthentication>&& frame) {
    debugbss("[client] [%s] received Deauthentication: %hu\n", client_->addr().ToString().c_str(),
             frame.body()->reason_code);
    bool send_deauth_frame = true;
    MoveToState<DeauthenticatingState>(
        static_cast<wlan_mlme::ReasonCode>(frame.body()->reason_code), send_deauth_frame);
}

void AssociatedState::HandleDisassociation(MgmtFrame<Disassociation>&& frame) {
    debugbss("[client] [%s] received Disassociation request: %u\n",
             client_->addr().ToString().c_str(), frame.body()->reason_code);
    service::SendDisassociateIndication(client_->device(), client_->addr(),
                                        frame.body()->reason_code);
    MoveToState<AuthenticatedState>();
}

void AssociatedState::HandlePsPollFrame(CtrlFrame<PsPollFrame>&& frame) {
    debugbss("[client] [%s] client requested BU\n", client_->addr().ToString().c_str());

    if (HasBufferedFrames()) {
        SendNextBu();
        return;
    }

    debugbss("[client] [%s] no more BU available\n", client_->addr().ToString().c_str());
    // There are no frames buffered for the client.
    // Respond with a null data frame and report the situation.
    auto packet = GetWlanPacket(DataFrameHeader::max_len());
    if (packet == nullptr) { return; }

    BufferWriter w(*packet);
    auto data_hdr = w.Write<DataFrameHeader>();
    data_hdr->fc.set_type(FrameType::kData);
    data_hdr->fc.set_subtype(DataSubtype::kNull);
    data_hdr->fc.set_from_ds(1);
    data_hdr->addr1 = client_->addr();
    data_hdr->addr2 = client_->bss()->bssid();
    data_hdr->addr3 = client_->bss()->bssid();
    data_hdr->sc.set_seq(client_->bss()->NextSns1(data_hdr->addr1));

    packet->set_len(w.WrittenBytes());

    zx_status_t status = client_->bss()->SendDataFrame(DataFrame<>(std::move(packet)),
                                                       WLAN_TX_INFO_FLAGS_FAVOR_RELIABILITY);
    if (status != ZX_OK) {
        errorf(
            "[client] [%s] could not send null data frame as PS-POLL response: "
            "%d\n",
            client_->addr().ToString().c_str(), status);
    }
}

std::optional<DataFrame<LlcHeader>> AssociatedState::EthToDataFrame(const EthFrame& eth_frame) {
    bool needs_protection =
        client_->bss()->IsRsn() && eapol_controlled_port_ == eapol::PortState::kOpen;
    return client_->bss()->EthToDataFrame(eth_frame, needs_protection);
}

void AssociatedState::OnExit() {
    client_->CancelTimer(inactive_timeout_);

    client_->device()->ClearAssoc(client_->addr());

    client_->ReportDisassociation(aid_);
    debugbss("[client] [%s] reported disassociation, AID: %u\n", client_->addr().ToString().c_str(),
             aid_);

    std::queue<EthFrame> empty_queue;
    std::swap(bu_queue_, empty_queue);
}

void AssociatedState::HandleDataLlcFrame(DataFrame<LlcHeader>&& frame) {
    if (frame.hdr()->fc.to_ds() == 0 || frame.hdr()->fc.from_ds() == 1) {
        warnf(
            "received unsupported data frame from %s with to_ds/from_ds "
            "combination: %u/%u\n",
            frame.hdr()->addr2.ToString().c_str(), frame.hdr()->fc.to_ds(),
            frame.hdr()->fc.from_ds());
        return;
    }

    auto data_llc_frame = frame.View();
    auto data_hdr = data_llc_frame.hdr();

    // Forward EAPOL frames to SME.
    auto llc_frame = data_llc_frame.SkipHeader();
    if (auto eapol_frame = llc_frame.CheckBodyType<EapolHdr>().CheckLength().SkipHeader()) {
        if (eapol_frame.body_len() == eapol_frame.hdr()->get_packet_body_length()) {
            service::SendEapolIndication(client_->device(), *eapol_frame.hdr(), data_hdr->addr2,
                                         data_hdr->addr3);
        }
        return;
    }

    // Block data frames if 802.1X authentication is required but didn't finish
    // yet.
    if (eapol_controlled_port_ != eapol::PortState::kOpen) { return; }

    size_t payload_len = llc_frame.body_len();
    size_t eth_frame_len = EthernetII::max_len() + payload_len;
    auto packet = GetEthPacket(eth_frame_len);
    if (packet == nullptr) { return; }

    BufferWriter w(*packet);
    auto eth_hdr = w.Write<EthernetII>();
    eth_hdr->dest = data_hdr->addr3;
    eth_hdr->src = data_hdr->addr2;
    eth_hdr->set_ether_type(llc_frame.hdr()->protocol_id());
    w.Write(llc_frame.body_data());

    packet->set_len(w.WrittenBytes());

    auto status = client_->bss()->DeliverEthernet(*packet);
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send ethernet data: %d\n",
               client_->addr().ToString().c_str(), status);
    }
}

zx_status_t AssociatedState::HandleMlmeMsg(const BaseMlmeMsg& msg) {
    if (auto eapol_request = msg.As<wlan_mlme::EapolRequest>()) {
        return HandleMlmeEapolReq(*eapol_request);
    } else if (auto deauth_req = msg.As<wlan_mlme::DeauthenticateRequest>()) {
        return HandleMlmeDeauthReq(*deauth_req);
    } else if (auto req = msg.As<wlan_mlme::SetControlledPortRequest>()) {
        ZX_DEBUG_ASSERT(client_->addr() == common::MacAddr(req->body()->peer_sta_address.data()));
        if (req->body()->state == wlan_mlme::ControlledPortState::OPEN) {
            eapol_controlled_port_ = eapol::PortState::kOpen;
        } else {
            eapol_controlled_port_ = eapol::PortState::kBlocked;
        }
        return ZX_OK;
    } else {
        warnf("[client] [%s] unexpected MLME msg type in associated state; ordinal: %u\n",
              client_->addr().ToString().c_str(), msg.ordinal());
        return ZX_ERR_INVALID_ARGS;
    }
}

void AssociatedState::HandleTimeout(TimeoutId id) {
    if (inactive_timeout_ != id) { return; }

    if (active_) {
        active_ = false;

        // Client was active, restart timer.
        debugbss("[client] [%s] client is active; reset inactive timer\n",
                 client_->addr().ToString().c_str());
        client_->ScheduleTimer(kInactivityTimeoutTu, &inactive_timeout_);
    } else {
        active_ = false;

        debugbss("[client] [%s] client inactive for %lu seconds; deauthenticating client\n",
                 client_->addr().ToString().c_str(), kInactivityTimeoutTu / 1000);
        bool send_deauth_frame = true;
        MoveToState<DeauthenticatingState>(wlan_mlme::ReasonCode::REASON_INACTIVITY,
                                           send_deauth_frame);
    }
}

void AssociatedState::UpdatePowerSaveMode(const FrameControl& fc) {
    if (eapol_controlled_port_ == eapol::PortState::kBlocked) { return; }

    active_ = true;

    if (fc.pwr_mgmt() != dozing_) {
        dozing_ = fc.pwr_mgmt();

        if (dozing_) {
            debugps("[client] [%s] client is now dozing\n", client_->addr().ToString().c_str());
        } else {
            debugps("[client] [%s] client woke up\n", client_->addr().ToString().c_str());

            // Send all buffered frames when client woke up.
            // TODO(hahnr): Once we implemented a smarter way of queuing packets, this
            // code should be revisited.
            while (HasBufferedFrames()) {
                auto status = SendNextBu();
                if (status != ZX_OK) { return; }
            }
        }
    }
}

zx_status_t AssociatedState::HandleMlmeEapolReq(const MlmeMsg<wlan_mlme::EapolRequest>& req) {
    size_t eapol_pdu_len = req.body()->data.size();
    size_t max_frame_len = DataFrameHeader::max_len() + LlcHeader::max_len() + eapol_pdu_len;
    auto packet = GetWlanPacket(max_frame_len);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    BufferWriter w(*packet);
    auto data_hdr = w.Write<DataFrameHeader>();
    data_hdr->fc.set_type(FrameType::kData);
    data_hdr->fc.set_from_ds(1);
    data_hdr->addr1.Set(req.body()->dst_addr.data());
    data_hdr->addr2 = client_->bss()->bssid();
    data_hdr->addr3.Set(req.body()->src_addr.data());
    data_hdr->sc.set_seq(client_->bss()->NextSns1(data_hdr->addr1));

    auto llc_hdr = w.Write<LlcHeader>();
    llc_hdr->dsap = kLlcSnapExtension;
    llc_hdr->ssap = kLlcSnapExtension;
    llc_hdr->control = kLlcUnnumberedInformation;
    std::memcpy(llc_hdr->oui, kLlcOui, sizeof(llc_hdr->oui));
    llc_hdr->set_protocol_id(kEapolProtocolId);
    w.Write({req.body()->data.data(), eapol_pdu_len});

    packet->set_len(w.WrittenBytes());

    auto status = client_->bss()->SendDataFrame(DataFrame<>(std::move(packet)),
                                                WLAN_TX_INFO_FLAGS_FAVOR_RELIABILITY);
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

zx_status_t AssociatedState::HandleMlmeDeauthReq(
    const MlmeMsg<wlan_mlme::DeauthenticateRequest>& req) {
    client_->SendDeauthentication(req.body()->reason_code);
    service::SendDeauthConfirm(client_->device(), client_->addr());
    MoveToState<DeauthenticatedState>(DeauthenticatedState::MoveReason::EXPLICIT_DEAUTH);
    return ZX_OK;
}

zx_status_t AssociatedState::SendNextBu() {
    ZX_DEBUG_ASSERT(HasBufferedFrames());
    if (!HasBufferedFrames()) { return ZX_ERR_BAD_STATE; }

    // Dequeue buffered Ethernet frame.
    auto eth_frame = DequeueEthernetFrame();
    if (!eth_frame) {
        errorf("[client] [%s] no more BU available\n", client_->addr().ToString().c_str());
        return ZX_ERR_BAD_STATE;
    }

    auto data_frame = EthToDataFrame(eth_frame.value());
    if (!data_frame) {
        errorf("[client] [%s] couldn't convert ethernet frame\n",
               client_->addr().ToString().c_str());
        return ZX_ERR_NO_RESOURCES;
    }

    // Set `more` bit if there are more frames buffered.
    data_frame->hdr()->fc.set_more_data(HasBufferedFrames());

    // Send Data frame.
    debugps("[client] [%s] sent BU to client\n", client_->addr().ToString().c_str());
    return client_->bss()->SendDataFrame(DataFrame<>(data_frame->Take()));
}

void AssociatedState::HandleActionFrame(MgmtFrame<ActionFrame>&& frame) {
    debugfn();

    auto action_frame = frame.View().NextFrame();
    if (auto action_ba_frame = action_frame.CheckBodyType<ActionFrameBlockAck>().CheckLength()) {
        auto ba_frame = action_ba_frame.NextFrame();
        if (auto add_ba_resp_frame = ba_frame.CheckBodyType<AddBaResponseFrame>().CheckLength()) {
            finspect("Inbound ADDBA Resp frame: len %zu\n", add_ba_resp_frame.body_len());
            finspect("  addba resp: %s\n", debug::Describe(*add_ba_resp_frame.body()).c_str());
            // TODO(porce): Handle AddBaResponses and keep the result of negotiation.
        } else if (auto add_ba_req_frame =
                       ba_frame.CheckBodyType<AddBaRequestFrame>().CheckLength()) {
            finspect("Inbound ADDBA Req frame: len %zu\n", add_ba_req_frame.body_len());
            finspect("  addba req: %s\n", debug::Describe(*add_ba_req_frame.body()).c_str());
            client_->SendAddBaResponse(*add_ba_req_frame.body());
        }
    }
}

zx_status_t AssociatedState::EnqueueEthernetFrame(EthFrame&& eth_frame) {
    // Drop oldest frame if queue reached its limit.
    if (bu_queue_.size() >= kMaxPowerSavingQueueSize) {
        bu_queue_.pop();
        warnf("[client] [%s] dropping oldest unicast frame\n", client_->addr().ToString().c_str());
    }

    debugps("[client] [%s] client is dozing; buffer outbound frame\n",
            client_->addr().ToString().c_str());

    bu_queue_.push(std::move(eth_frame));
    client_->ReportBuChange(aid_, bu_queue_.size());

    return ZX_OK;
}

std::optional<EthFrame> AssociatedState::DequeueEthernetFrame() {
    if (bu_queue_.empty()) { return {}; }

    auto eth_frame = std::move(bu_queue_.front());
    bu_queue_.pop();
    client_->ReportBuChange(aid_, bu_queue_.size());
    return std::move(eth_frame);
}

bool AssociatedState::HasBufferedFrames() const {
    return bu_queue_.size() > 0;
}

// RemoteClient implementation.

RemoteClient::RemoteClient(DeviceInterface* device, BssInterface* bss,
                           RemoteClient::Listener* listener, const common::MacAddr& addr)
    : listener_(listener), device_(device), bss_(bss), addr_(addr), is_qos_ready_(false) {
    ZX_DEBUG_ASSERT(device_ != nullptr);
    ZX_DEBUG_ASSERT(bss_ != nullptr);
    debugbss("[client] [%s] spawned\n", addr_.ToString().c_str());

    MoveToState(
        fbl::make_unique<DeauthenticatedState>(this, DeauthenticatedState::MoveReason::INIT));
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
    state_ = std::move(to);

    state_->OnEnter();
}

void RemoteClient::HandleTimeout(TimeoutId id) {
    state_->HandleTimeout(id);
}

void RemoteClient::HandleAnyEthFrame(EthFrame&& frame) {
    state_->HandleEthFrame(std::move(frame));
}

void RemoteClient::HandleAnyMgmtFrame(MgmtFrame<>&& frame) {
    state_->HandleAnyMgmtFrame(std::move(frame));
}

void RemoteClient::HandleAnyDataFrame(DataFrame<>&& frame) {
    state_->HandleAnyDataFrame(std::move(frame));
}

void RemoteClient::HandleAnyCtrlFrame(CtrlFrame<>&& frame) {
    state_->HandleAnyCtrlFrame(std::move(frame));
}

zx_status_t RemoteClient::HandleMlmeMsg(const BaseMlmeMsg& msg) {
    return state_->HandleMlmeMsg(msg);
}

zx_status_t RemoteClient::ScheduleTimer(wlan_tu_t tus, TimeoutId* id) {
    return bss_->ScheduleTimeout(tus, addr_, id);
}

void RemoteClient::CancelTimer(TimeoutId id) {
    return bss_->CancelTimeout(id);
}

zx_status_t RemoteClient::SendAuthentication(wlan_status_code result) {
    debugfn();
    debugbss("[client] [%s] sending Authentication response\n", addr_.ToString().c_str());

    size_t max_frame_size = MgmtFrameHeader::max_len() + Authentication::max_len();
    auto packet = GetWlanPacket(max_frame_size);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    BufferWriter w(*packet);
    auto mgmt_hdr = w.Write<MgmtFrameHeader>();
    mgmt_hdr->fc.set_type(FrameType::kManagement);
    mgmt_hdr->fc.set_subtype(ManagementSubtype::kAuthentication);
    mgmt_hdr->addr1 = addr_;
    mgmt_hdr->addr2 = bss_->bssid();
    mgmt_hdr->addr3 = bss_->bssid();
    mgmt_hdr->sc.set_seq(bss_->NextSns1(mgmt_hdr->addr1));

    auto auth = w.Write<Authentication>();
    auth->status_code = result;
    auth->auth_algorithm_number = AuthAlgorithm::kOpenSystem;
    // TODO(hahnr): Evolve this to support other authentication algorithms and
    // track seq number.
    auth->auth_txn_seq_number = 2;

    packet->set_len(w.WrittenBytes());

    auto status = bss_->SendMgmtFrame(MgmtFrame<>(std::move(packet)));
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send auth response packet: %d\n", addr_.ToString().c_str(),
               status);
    }
    return status;
}

zx_status_t RemoteClient::SendAssociationResponse(aid_t aid, wlan_status_code result) {
    debugfn();
    debugbss("[client] [%s] sending Association Response\n", addr_.ToString().c_str());

    size_t reserved_ie_len = 256;
    size_t max_frame_size =
        MgmtFrameHeader::max_len() + AssociationResponse::max_len() + reserved_ie_len;
    auto packet = GetWlanPacket(max_frame_size);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    BufferWriter w(*packet);
    auto mgmt_hdr = w.Write<MgmtFrameHeader>();
    mgmt_hdr->fc.set_type(FrameType::kManagement);
    mgmt_hdr->fc.set_subtype(ManagementSubtype::kAssociationResponse);
    mgmt_hdr->addr1 = addr_;
    mgmt_hdr->addr2 = bss_->bssid();
    mgmt_hdr->addr3 = bss_->bssid();
    mgmt_hdr->sc.set_seq(bss_->NextSns1(mgmt_hdr->addr1));

    auto assoc = w.Write<AssociationResponse>();
    assoc->status_code = result;
    assoc->aid = aid;
    assoc->cap.set_ess(1);
    assoc->cap.set_short_preamble(1);

    // Write elements.
    BufferWriter elem_w(w.RemainingBuffer());
    RatesWriter rates_writer(bss_->Rates());
    rates_writer.WriteSupportedRates(&elem_w);
    rates_writer.WriteExtendedSupportedRates(&elem_w);

    auto ht = bss_->Ht();
    if (ht.ready) {
        common::WriteHtCapabilities(&elem_w, BuildHtCapabilities(ht));
        common::WriteHtOperation(&elem_w, BuildHtOperation(bss_->Chan()));
    }

    packet->set_len(w.WrittenBytes() + elem_w.WrittenBytes());

    auto status = bss_->SendMgmtFrame(MgmtFrame<>(std::move(packet)));
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send auth response packet: %d\n", addr_.ToString().c_str(),
               status);
    }
    return status;
}

zx_status_t RemoteClient::SendDeauthentication(wlan_mlme::ReasonCode reason_code) {
    debugfn();
    debugbss("[client] [%s] sending Deauthentication\n", addr_.ToString().c_str());

    size_t max_frame_size = MgmtFrameHeader::max_len() + Deauthentication::max_len();
    auto packet = GetWlanPacket(max_frame_size);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    BufferWriter w(*packet);
    auto mgmt_hdr = w.Write<MgmtFrameHeader>();
    mgmt_hdr->fc.set_type(FrameType::kManagement);
    mgmt_hdr->fc.set_subtype(ManagementSubtype::kDeauthentication);
    mgmt_hdr->addr1 = addr_;
    mgmt_hdr->addr2 = bss_->bssid();
    mgmt_hdr->addr3 = bss_->bssid();
    mgmt_hdr->sc.set_seq(bss_->NextSns1(mgmt_hdr->addr1));

    w.Write<Deauthentication>()->reason_code = static_cast<uint16_t>(reason_code);

    packet->set_len(w.WrittenBytes());

    auto status = bss_->SendMgmtFrame(MgmtFrame<>(std::move(packet)));
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send dauthentication packet: %d\n",
               addr_.ToString().c_str(), status);
    }
    return status;
}

void RemoteClient::ReportBuChange(aid_t aid, size_t bu_count) {
    if (listener_ != nullptr) { listener_->HandleClientBuChange(addr_, aid, bu_count); }
}

void RemoteClient::ReportFailedAuth() {
    if (listener_ != nullptr) { listener_->HandleClientFailedAuth(addr_); }
}

void RemoteClient::ReportDeauthentication() {
    if (listener_ != nullptr) { listener_->HandleClientDeauth(addr_); }
}

void RemoteClient::ReportDisassociation(aid_t aid) {
    if (listener_ != nullptr) { listener_->HandleClientDisassociation(aid); }
}

uint8_t RemoteClient::GetTid() {
    // TODO(NET-599): Implement QoS policy engine.
    return 0;
}

zx_status_t RemoteClient::SendAddBaRequest() {
    debugfn();
    if (!bss_->Ht().ready) { return ZX_OK; }

    debugbss("[client] [%s] sending AddBaRequest\n", addr_.ToString().c_str());

    size_t max_frame_size = MgmtFrameHeader::max_len() + ActionFrame::max_len() +
                            ActionFrameBlockAck::max_len() + AddBaRequestFrame::max_len();
    auto packet = GetWlanPacket(max_frame_size);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    BufferWriter w(*packet);
    auto mgmt_hdr = w.Write<MgmtFrameHeader>();
    mgmt_hdr->fc.set_type(FrameType::kManagement);
    mgmt_hdr->fc.set_subtype(ManagementSubtype::kAction);
    mgmt_hdr->addr1 = addr_;
    mgmt_hdr->addr2 = bss_->bssid();
    mgmt_hdr->addr3 = bss_->bssid();
    mgmt_hdr->sc.set_seq(bss_->NextSns1(mgmt_hdr->addr1));

    w.Write<ActionFrame>()->category = action::Category::kBlockAck;
    w.Write<ActionFrameBlockAck>()->action = action::BaAction::kAddBaRequest;

    auto addbareq_hdr = w.Write<AddBaRequestFrame>();
    // It appears there is no particular rule to choose the value for
    // dialog_token. See IEEE Std 802.11-2016, 9.6.5.2.
    addbareq_hdr->dialog_token = 0x01;
    addbareq_hdr->params.set_amsdu(0);
    addbareq_hdr->params.set_policy(BlockAckParameters::BlockAckPolicy::kImmediate);
    addbareq_hdr->params.set_tid(GetTid());  // TODO(NET-599): Communicate this with lower MAC.
    // TODO(porce): Fix the discrepancy of this value from the Ralink's TXWI ba_win_size setting
    addbareq_hdr->params.set_buffer_size(64);
    addbareq_hdr->timeout = 0;               // Disables the timeout
    addbareq_hdr->seq_ctrl.set_fragment(0);  // TODO(NET-599): Send this down to the lower MAC
    addbareq_hdr->seq_ctrl.set_starting_seq(1);

    packet->set_len(w.WrittenBytes());

    finspect("Outbound ADDBA Req frame: len %zu\n", w.WrittenBytes());
    finspect("  addba req: %s\n", debug::Describe(*addbareq_hdr).c_str());

    auto status = bss_->SendMgmtFrame(MgmtFrame<>(std::move(packet)));
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send AddbaRequest: %d\n", addr_.ToString().c_str(), status);
    }

    return ZX_OK;
}

zx_status_t RemoteClient::SendAddBaResponse(const AddBaRequestFrame& req) {
    size_t max_frame_size = MgmtFrameHeader::max_len() + ActionFrame::max_len() +
                            ActionFrameBlockAck::max_len() + AddBaRequestFrame::max_len();
    auto packet = GetWlanPacket(max_frame_size);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    BufferWriter w(*packet);
    auto mgmt_hdr = w.Write<MgmtFrameHeader>();
    mgmt_hdr->fc.set_type(FrameType::kManagement);
    mgmt_hdr->fc.set_subtype(ManagementSubtype::kAction);
    mgmt_hdr->addr1 = addr_;
    mgmt_hdr->addr2 = bss_->bssid();
    mgmt_hdr->addr3 = bss_->bssid();
    mgmt_hdr->sc.set_seq(bss_->NextSns1(mgmt_hdr->addr1));

    w.Write<ActionFrame>()->category = action::Category::kBlockAck;
    w.Write<ActionFrameBlockAck>()->action = action::BaAction::kAddBaResponse;

    auto addbaresp_hdr = w.Write<AddBaResponseFrame>();
    addbaresp_hdr->dialog_token = req.dialog_token;
    // TODO(porce): Implement DelBa as a response to AddBar for decline
    addbaresp_hdr->status_code = WLAN_STATUS_CODE_SUCCESS;
    // TODO(NET-567): Use the outcome of the association negotiation
    addbaresp_hdr->params.set_amsdu(0);
    addbaresp_hdr->params.set_policy(BlockAckParameters::kImmediate);
    addbaresp_hdr->params.set_tid(req.params.tid());
    // TODO(NET-565, NET-567): Use the chipset's buffer_size
    auto buffer_size_ap = req.params.buffer_size();
    constexpr size_t buffer_size_ralink = 64;
    auto buffer_size = (buffer_size_ap <= buffer_size_ralink) ? buffer_size_ap : buffer_size_ralink;
    addbaresp_hdr->params.set_buffer_size(buffer_size);
    addbaresp_hdr->timeout = req.timeout;

    packet->set_len(w.WrittenBytes());

    finspect("Outbound ADDBA Resp frame: len %zu\n", w.WrittenBytes());
    finspect("Outbound Mgmt Frame(ADDBA Resp): %s\n", debug::Describe(*addbaresp_hdr).c_str());

    auto status = bss_->SendMgmtFrame(MgmtFrame<>(std::move(packet)));
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send AddBaResponse: %d\n", addr_.ToString().c_str(),
               status);
        return status;
    }

    return ZX_OK;
}

wlan_assoc_ctx_t RemoteClient::BuildAssocContext(uint16_t aid) {
    wlan_assoc_ctx_t assoc;
    memset(&assoc, 0, sizeof(assoc));

    addr().CopyTo(assoc.bssid);
    assoc.aid = aid;

    assoc.listen_interval = 3;  // The listen interval is not really useful for remote client (as
                                // AP role). The field is mainly for client role. (Maybe we need it
                                // in the future for Mesh role. Don't know yet) Thus, hard-code a
                                // number here for ath10k AP mode only. See NET-1816.
    assoc.phy = WLAN_PHY_ERP;   // Default vlaue. Will be overwritten below.
    assoc.chan = bss_->Chan();

    auto rates = bss_->Rates();
    assoc.rates_cnt = std::min(rates.size(), static_cast<size_t>(WLAN_MAC_MAX_RATES));
    if (assoc.rates_cnt != rates.size()) {
        warnf("num_rates is truncated from %zu to %d", rates.size(), WLAN_MAC_MAX_RATES);
    }

    std::copy(rates.cbegin(), rates.cend(), assoc.rates);

    auto ht = bss_->Ht();
    if (ht.ready) {
        assoc.has_ht_cap = true;
        assoc.phy = WLAN_PHY_HT;
        HtCapabilities ht_cap = BuildHtCapabilities(ht);

        assoc.ht_cap = ht_cap.ToDdk();
    }

    // TODO(NET-1708): Support VHT MSC

    // If the client supports either HT or VHT, tell the driver to send out with QoS header (if the
    // driver/firmware supports it).
    if (assoc.has_ht_cap || assoc.has_vht_cap) {
        assoc.qos = true;
        is_qos_ready_ = true;
    }

    return assoc;
}

}  // namespace wlan
