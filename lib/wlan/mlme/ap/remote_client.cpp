// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/ap/remote_client.h>

#include <wlan/common/write_element.h>
#include <wlan/mlme/debug.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/rates_elements.h>
#include <wlan/mlme/service.h>

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
    client_->ReportDeauthentication();
    service::SendDeauthIndication(client_->device(), client_->addr(), reason_code_);
    if (send_deauth_frame_) { client_->SendDeauthentication(reason_code_); }
    MoveToState<DeauthenticatedState>();
}

// DeauthenticatedState implementation.

DeauthenticatedState::DeauthenticatedState(RemoteClient* client) : BaseState(client) {}

void DeauthenticatedState::HandleAnyMgmtFrame(MgmtFrame<>&& frame) {
    if (auto auth = frame.View().CheckBodyType<Authentication>().CheckLength()) {
        MoveToState<AuthenticatingState>(auth.IntoOwned(frame.Take()));
    }
}

// AuthenticatingState implementation.

AuthenticatingState::AuthenticatingState(RemoteClient* client, MgmtFrame<Authentication>&& frame)
    : BaseState(client) {
    ZX_DEBUG_ASSERT(frame.hdr()->addr2 == client_->addr());
    debugbss("[client] [%s] received Authentication request...\n",
             client_->addr().ToString().c_str());

    auto auth_alg = frame.body()->auth_algorithm_number;
    if (auth_alg != AuthAlgorithm::kOpenSystem) {
        errorf("[client] [%s] received auth attempt with unsupported algorithm: %u\n",
               client_->addr().ToString().c_str(), auth_alg);
        FinalizeAuthenticationAttempt(status_code::kUnsupportedAuthAlgorithm);
        return;
    }

    auto auth_txn_seq_no = frame.body()->auth_txn_seq_number;
    if (auth_txn_seq_no != 1) {
        errorf("[client] [%s] received auth attempt with invalid tx seq no: %u\n",
               client_->addr().ToString().c_str(), auth_txn_seq_no);
        FinalizeAuthenticationAttempt(status_code::kRefused);
        return;
    }

    service::SendAuthIndication(client_->device(), client_->addr(),
                                wlan_mlme::AuthenticationTypes::OPEN_SYSTEM);
}

void AuthenticatingState::OnEnter() {
    auto deadline = client_->DeadlineAfterTus(kAuthenticatingTimeoutTu);
    client_->ScheduleTimer(deadline, &auth_timeout_);
}

void AuthenticatingState::OnExit() {
    auth_timeout_.Cancel();
}

void AuthenticatingState::HandleTimeout(zx::time now) {
    if (auth_timeout_.Triggered(now)) {
        auth_timeout_.Cancel();
        warnf("[client] [%s] timed out authenticating\n", client_->addr().ToString().c_str());
        client_->ReportFailedAuth();
        MoveToState<DeauthenticatedState>();
    }
}

zx_status_t AuthenticatingState::HandleMlmeMsg(const BaseMlmeMsg& msg) {
    if (auto auth_resp = msg.As<wlan_mlme::AuthenticateResponse>()) {
        ZX_DEBUG_ASSERT(client_->addr() ==
                        common::MacAddr(auth_resp->body()->peer_sta_address.data()));
        // Received request which we've been waiting for. Timer can get canceled.
        auth_timeout_.Cancel();

        status_code::StatusCode st_code;
        if (auth_resp->body()->result_code == wlan_mlme::AuthenticateResultCodes::SUCCESS) {
            st_code = status_code::kSuccess;
        } else {
            // TODO(NET-1464): map result code to status code;
            st_code = status_code::kRefused;
        }
        return FinalizeAuthenticationAttempt(st_code);
    } else {
        warnf("[client] [%s] unexpected MLME msg type in authenticating state; ordinal: %u\n",
              client_->addr().ToString().c_str(), msg.ordinal());
        return ZX_ERR_INVALID_ARGS;
    }
}

zx_status_t AuthenticatingState::FinalizeAuthenticationAttempt(
    const status_code::StatusCode st_code) {
    bool auth_success = st_code == status_code::kSuccess;
    auto status = client_->SendAuthentication(st_code);
    if (auth_success && status == ZX_OK) {
        MoveToState<AuthenticatedState>();
    } else {
        client_->ReportFailedAuth();
        MoveToState<DeauthenticatedState>();
    }
    return status;
}

// AuthenticatedState implementation.

AuthenticatedState::AuthenticatedState(RemoteClient* client) : BaseState(client) {}

void AuthenticatedState::OnEnter() {
    // Start timeout and wait for Association requests.
    auto deadline = client_->DeadlineAfterTus(kAuthenticationTimeoutTu);
    client_->ScheduleTimer(deadline, &auth_timeout_);
}

void AuthenticatedState::OnExit() {
    auth_timeout_.Cancel();
}

void AuthenticatedState::HandleTimeout(zx::time now) {
    if (auth_timeout_.Triggered(now)) {
        auth_timeout_.Cancel();
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
    MoveToState<AuthenticatingState>(fbl::move(frame));
}

void AuthenticatedState::HandleDeauthentication(MgmtFrame<Deauthentication>&& frame) {
    debugbss("[client] [%s] received Deauthentication: %hu\n", client_->addr().ToString().c_str(),
             frame.body()->reason_code);
    bool send_deauth_frame = false;
    MoveToState<DeauthenticatingState>(
        static_cast<wlan_mlme::ReasonCode>(frame.body()->reason_code), send_deauth_frame);
}

void AuthenticatedState::HandleAssociationRequest(MgmtFrame<AssociationRequest>&& frame) {
    // Received request which we've been waiting for. Timer can get canceled.
    auth_timeout_.Cancel();

    // Move into Associating state state which responds to incoming Association
    // requests.
    MoveToState<AssociatingState>(fbl::move(frame));
}

// AssociatingState implementation.

AssociatingState::AssociatingState(RemoteClient* client, MgmtFrame<AssociationRequest>&& frame)
    : BaseState(client), aid_(kUnknownAid) {
    debugfn();
    ZX_DEBUG_ASSERT(frame.hdr()->addr2 == client_->addr());
    debugbss("[client] [%s] received Assocation Request\n", client_->addr().ToString().c_str());

    auto assoc_req_frame = frame.View().NextFrame();
    size_t elements_len = assoc_req_frame.body_len();
    ElementReader reader(assoc_req_frame.hdr()->elements, elements_len);

    const SsidElement* ssid_element = nullptr;
    const RsnElement* rsn_element = nullptr;
    while (reader.is_valid()) {
        const ElementHeader* header = reader.peek();
        ZX_DEBUG_ASSERT(header != nullptr);
        switch (header->id) {
        case element_id::kSsid:
            ssid_element = reader.read<SsidElement>();
            break;
        case element_id::kRsn:
            rsn_element = reader.read<RsnElement>();
            break;
        default:
            reader.skip(*header);
            break;
        }
    }

    ZX_DEBUG_ASSERT(ssid_element != nullptr);

    service::SendAssocIndication(client_->device(), client_->addr(), frame.body()->listen_interval,
                                 *ssid_element, rsn_element);
}

void AssociatingState::OnEnter() {
    auto deadline = client_->DeadlineAfterTus(kAssociatingTimeoutTu);
    client_->ScheduleTimer(deadline, &assoc_timeout_);
}

void AssociatingState::OnExit() {
    assoc_timeout_.Cancel();
}

void AssociatingState::HandleTimeout(zx::time now) {
    if (assoc_timeout_.Triggered(now)) {
        assoc_timeout_.Cancel();
        warnf("[client] [%s] timed out associating\n", client_->addr().ToString().c_str());
        MoveToState<AuthenticatedState>();
    }
}

zx_status_t AssociatingState::HandleMlmeMsg(const BaseMlmeMsg& msg) {
    if (auto assoc_resp = msg.As<wlan_mlme::AssociateResponse>()) {
        ZX_DEBUG_ASSERT(client_->addr() ==
                        common::MacAddr(assoc_resp->body()->peer_sta_address.data()));
        // Received request which we've been waiting for. Timer can get canceled.
        assoc_timeout_.Cancel();

        status_code::StatusCode st_code;
        if (assoc_resp->body()->result_code == wlan_mlme::AssociateResultCodes::SUCCESS) {
            aid_ = assoc_resp->body()->association_id;
            st_code = status_code::kSuccess;
        } else {
            // TODO(NET-1464): map result code to status code;
            st_code = status_code::kRefused;
        }
        return FinalizeAssociationAttempt(st_code);
    } else {
        warnf("[client] [%s] unexpected MLME msg type in associating state; ordinal: %u\n",
              client_->addr().ToString().c_str(), msg.ordinal());
        return ZX_ERR_INVALID_ARGS;
    }
}

zx_status_t AssociatingState::FinalizeAssociationAttempt(status_code::StatusCode st_code) {
    bool assoc_success = st_code == status_code::kSuccess;
    auto status = client_->SendAssociationResponse(aid_, st_code);
    if (assoc_success && status == ZX_OK) {
        MoveToState<AssociatedState>(aid_);
    } else {
        service::SendDisassociateIndication(client_->device(), client_->addr(),
                                            reason_code::ReasonCode::kUnspecifiedReason);
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
    // Client believes it is not yet authenticated. Thus, there is no need to send
    // an explicit Deauthentication.
    MoveToState<AuthenticatingState>(fbl::move(frame));
}

void AssociatedState::HandleAssociationRequest(MgmtFrame<AssociationRequest>&& frame) {
    debugfn();
    ZX_DEBUG_ASSERT(frame.hdr()->addr2 == client_->addr());
    debugbss("[client] [%s] received Assocation Request while being associated\n",
             client_->addr().ToString().c_str());
    // Client believes it is not yet associated. Thus, there is no need to send an
    // explicit Deauthentication.
    MoveToState<AssociatingState>(fbl::move(frame));
}

void AssociatedState::OnEnter() {
    debugbss("[client] [%s] acquired AID: %u\n", client_->addr().ToString().c_str(), aid_);

    auto deadline = client_->DeadlineAfterTus(kInactivityTimeoutTu);
    client_->ScheduleTimer(deadline, &inactive_timeout_);
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
        auto status = EnqueueEthernetFrame(fbl::move(eth_frame));
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
    client_->bss()->SendDataFrame(data_frame->Generalize());
}

void AssociatedState::OpenControlledPort() {
    eapol_controlled_port_ = eapol::PortState::kOpen;
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
    packet->clear();

    DataFrame<NullDataHdr> data_frame(fbl::move(packet));
    auto data_hdr = data_frame.hdr();
    data_hdr->fc.set_type(FrameType::kData);
    data_hdr->fc.set_subtype(DataSubtype::kNull);
    data_hdr->fc.set_from_ds(1);
    data_hdr->addr1 = client_->addr();
    data_hdr->addr2 = client_->bss()->bssid();
    data_hdr->addr3 = client_->bss()->bssid();
    data_hdr->sc.set_seq(client_->bss()->NextSeq(*data_hdr));

    data_frame.set_body_len(0);
    zx_status_t status = client_->bss()->SendDataFrame(data_frame.Generalize());
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
    inactive_timeout_.Cancel();

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

    EthFrame eth_frame(fbl::move(packet));
    eth_frame.hdr()->dest = data_hdr->addr3;
    eth_frame.hdr()->src = data_hdr->addr2;
    eth_frame.hdr()->ether_type = llc_frame.hdr()->protocol_id;

    std::memcpy(eth_frame.body()->data, llc_frame.body()->data, payload_len);

    auto status = client_->bss()->SendEthFrame(fbl::move(eth_frame));
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send ethernet data: %d\n",
               client_->addr().ToString().c_str(), status);
    }
}

zx_status_t AssociatedState::HandleMlmeMsg(const BaseMlmeMsg& msg) {
    if (auto eapol_request = msg.As<wlan_mlme::EapolRequest>()) {
        return HandleMlmeEapolReq(*eapol_request);
    } else {
        warnf("[client] [%s] unexpected MLME msg type in associated state; ordinal: %u\n",
              client_->addr().ToString().c_str(), msg.ordinal());
        return ZX_ERR_INVALID_ARGS;
    }
}

void AssociatedState::HandleTimeout(zx::time now) {
    if (!inactive_timeout_.Triggered(now)) { return; }
    inactive_timeout_.Cancel();

    if (active_) {
        active_ = false;

        // Client was active, restart timer.
        debugbss("[client] [%s] client is active; reset inactive timer\n",
                 client_->addr().ToString().c_str());
        auto deadline = client_->DeadlineAfterTus(kInactivityTimeoutTu);
        client_->ScheduleTimer(deadline, &inactive_timeout_);
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
    size_t eapol_pdu_len = req.body()->data->size();
    size_t max_frame_len = DataFrameHeader::max_len() + LlcHeader::max_len() + eapol_pdu_len;
    auto packet = GetWlanPacket(max_frame_len);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }
    packet->clear();

    DataFrame<LlcHeader> data_frame(fbl::move(packet));
    auto data_hdr = data_frame.hdr();
    data_hdr->fc.set_type(FrameType::kData);
    data_hdr->fc.set_from_ds(1);
    data_hdr->addr1.Set(req.body()->dst_addr.data());
    data_hdr->addr2 = client_->bss()->bssid();
    data_hdr->addr3.Set(req.body()->src_addr.data());
    data_hdr->sc.set_seq(client_->bss()->NextSeq(*data_hdr));

    auto llc_hdr = data_frame.body();
    llc_hdr->dsap = kLlcSnapExtension;
    llc_hdr->ssap = kLlcSnapExtension;
    llc_hdr->control = kLlcUnnumberedInformation;
    std::memcpy(llc_hdr->oui, kLlcOui, sizeof(llc_hdr->oui));
    llc_hdr->protocol_id = htobe16(kEapolProtocolId);
    std::memcpy(llc_hdr->payload, req.body()->data->data(), eapol_pdu_len);

    data_frame.set_body_len(llc_hdr->len() + eapol_pdu_len);
    auto status = client_->bss()->SendDataFrame(data_frame.Generalize());
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
    return client_->bss()->SendDataFrame(data_frame->Generalize());
}

void AssociatedState::HandleActionFrame(MgmtFrame<ActionFrame>&& frame) {
    debugfn();

    // TODO(porce): Handle AddBaResponses and keep the result of negotiation.

    auto action_frame = frame.View().NextFrame();
    if (auto action_ba_frame = action_frame.CheckBodyType<ActionFrameBlockAck>().CheckLength()) {
        auto ba_frame = action_ba_frame.NextFrame();
        if (auto add_ba_resp_frame = ba_frame.CheckBodyType<AddBaResponseFrame>().CheckLength()) {
            finspect("Inbound ADDBA Resp frame: len %zu\n", add_ba_resp_frame.body_len());
            finspect("  addba resp: %s\n", debug::Describe(*add_ba_resp_frame.body()).c_str());
        } else if (auto add_ba_req_frame =
                       ba_frame.CheckBodyType<AddBaRequestFrame>().CheckLength()) {
            finspect("Inbound ADDBA Req frame: len %zu\n", add_ba_req_frame.body_len());
            finspect("  addba req: %s\n", debug::Describe(*add_ba_req_frame.body()).c_str());
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

    bu_queue_.push(fbl::move(eth_frame));
    client_->ReportBuChange(aid_, bu_queue_.size());

    return ZX_OK;
}

std::optional<EthFrame> AssociatedState::DequeueEthernetFrame() {
    if (bu_queue_.empty()) { return std::nullopt; }

    auto eth_frame = std::move(bu_queue_.front());
    bu_queue_.pop();
    client_->ReportBuChange(aid_, bu_queue_.size());
    return std::make_optional(std::move(eth_frame));
}

bool AssociatedState::HasBufferedFrames() const {
    return bu_queue_.size() > 0;
}

// RemoteClient implementation.

RemoteClient::RemoteClient(DeviceInterface* device, TimerManager&& timer_mgr, BssInterface* bss,
                           RemoteClient::Listener* listener, const common::MacAddr& addr)
    : listener_(listener),
      device_(device),
      bss_(bss),
      addr_(addr),
      timer_mgr_(std::move(timer_mgr)) {
    ZX_DEBUG_ASSERT(device_ != nullptr);
    ZX_DEBUG_ASSERT(timer_mgr_.timer() != nullptr);
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
    zx::time now = timer_mgr_.HandleTimeout();
    state_->HandleTimeout(now);
}

void RemoteClient::HandleAnyEthFrame(EthFrame&& frame) {
    state_->HandleEthFrame(fbl::move(frame));
}

void RemoteClient::HandleAnyMgmtFrame(MgmtFrame<>&& frame) {
    state_->HandleAnyMgmtFrame(fbl::move(frame));
}

void RemoteClient::HandleAnyDataFrame(DataFrame<>&& frame) {
    state_->HandleAnyDataFrame(fbl::move(frame));
}

void RemoteClient::HandleAnyCtrlFrame(CtrlFrame<>&& frame) {
    state_->HandleAnyCtrlFrame(fbl::move(frame));
}

zx_status_t RemoteClient::HandleMlmeMsg(const BaseMlmeMsg& msg) {
    return state_->HandleMlmeMsg(msg);
}

void RemoteClient::OpenControlledPort() {
    state_->OpenControlledPort();
}

zx_status_t RemoteClient::ScheduleTimer(zx::time deadline, TimedEvent* event) {
    return timer_mgr_.Schedule(deadline, event);
}

zx::time RemoteClient::DeadlineAfterTus(wlan_tu_t tus) {
    return timer_mgr_.Now() + WLAN_TU(tus);
}

zx_status_t RemoteClient::SendAuthentication(status_code::StatusCode result) {
    debugfn();
    debugbss("[client] [%s] sending Authentication response\n", addr_.ToString().c_str());

    MgmtFrame<Authentication> frame;
    auto status = CreateMgmtFrame(&frame);
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

    status = bss_->SendMgmtFrame(frame.Generalize());
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send auth response packet: %d\n", addr_.ToString().c_str(),
               status);
    }
    return status;
}

zx_status_t RemoteClient::SendAssociationResponse(aid_t aid, status_code::StatusCode result) {
    debugfn();
    debugbss("[client] [%s] sending Association Response\n", addr_.ToString().c_str());

    size_t reserved_ie_len = 256;
    MgmtFrame<AssociationResponse> frame;
    auto status = CreateMgmtFrame(&frame, reserved_ie_len);
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
    BufferWriter w({assoc->elements, reserved_ie_len});

    size_t num_rates;
    auto* rates = bss_->Rates(&num_rates);

    RatesWriter rates_writer {{ rates, num_rates }};

    rates_writer.WriteSupportedRates(&w);
    rates_writer.WriteExtendedSupportedRates(&w);

    // TODO(NET-567): Write negotiated SupportedRates, ExtendedSupportedRates IEs

    auto ht = bss_->Ht();
    if (ht.ready) {
        common::WriteHtCapabilities(&w, BuildHtCapabilities(ht));
        common::WriteHtOperation(&w, BuildHtOperation(bss_->Chan()));
    }

    // Validate the request in debug mode.
    ZX_DEBUG_ASSERT(assoc->Validate(w.WrittenBytes()));

    size_t body_len = frame.body()->len() + w.WrittenBytes();
    status = frame.set_body_len(body_len);
    if (status != ZX_OK) {
        errorf("[client] [%s] could not set assocresp length to %zu: %d\n",
               addr_.ToString().c_str(), body_len, status);
        return status;
    }

    status = bss_->SendMgmtFrame(frame.Generalize());
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send auth response packet: %d\n", addr_.ToString().c_str(),
               status);
    }
    return status;
}

zx_status_t RemoteClient::SendDeauthentication(wlan_mlme::ReasonCode reason_code) {
    debugfn();
    debugbss("[client] [%s] sending Deauthentication\n", addr_.ToString().c_str());

    MgmtFrame<Deauthentication> frame;
    auto status = CreateMgmtFrame(&frame);
    if (status != ZX_OK) { return status; }

    auto hdr = frame.hdr();
    hdr->addr1 = addr_;
    hdr->addr2 = bss_->bssid();
    hdr->addr3 = bss_->bssid();
    hdr->sc.set_seq(bss_->NextSeq(*hdr));

    auto deauth = frame.body();
    deauth->reason_code = static_cast<uint16_t>(reason_code);

    status = bss_->SendMgmtFrame(frame.Generalize());
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

    MgmtFrame<ActionFrame> mgmt_frame;
    size_t body_payload_len = ActionFrameBlockAck::max_len() + AddBaRequestFrame::max_len();
    auto status = CreateMgmtFrame(&mgmt_frame, body_payload_len);
    if (status != ZX_OK) { return status; }

    auto hdr = mgmt_frame.hdr();
    hdr->addr1 = addr_;
    hdr->addr2 = bss_->bssid();
    hdr->addr3 = bss_->bssid();
    hdr->sc.set_seq(bss_->NextSeq(*hdr));
    mgmt_frame.FillTxInfo();

    auto action_hdr = mgmt_frame.body();
    action_hdr->category = ActionFrameBlockAck::ActionCategory();

    auto ba_frame = mgmt_frame.NextFrame<ActionFrameBlockAck>();
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

    status = bss_->SendMgmtFrame(MgmtFrame<>(addbareq_frame.Take()));
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send AddbaRequest: %d\n", addr_.ToString().c_str(), status);
    }

    return ZX_OK;
}

zx_status_t RemoteClient::SendAddBaResponse(const AddBaRequestFrame& req) {
    MgmtFrame<ActionFrame> mgmt_frame;
    size_t body_payload_len = ActionFrameBlockAck::max_len() + AddBaRequestFrame::max_len();
    auto status = CreateMgmtFrame(&mgmt_frame, body_payload_len);
    if (status != ZX_OK) { return status; }

    auto hdr = mgmt_frame.hdr();
    hdr->addr1 = addr_;
    hdr->addr2 = bss_->bssid();
    hdr->addr3 = bss_->bssid();
    hdr->sc.set_seq(bss_->NextSeq(*hdr));
    mgmt_frame.FillTxInfo();

    auto action_frame = mgmt_frame.body();
    action_frame->category = ActionFrameBlockAck::ActionCategory();

    auto ba_frame = mgmt_frame.NextFrame<ActionFrameBlockAck>();
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

    status = bss_->SendMgmtFrame(MgmtFrame<>(addbaresp_frame.Take()));
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

    const SupportedRate* rates;
    size_t num_rates;
    rates = bss_->Rates(&num_rates);
    assoc.supported_rates_cnt = num_rates;
    memcpy(assoc.supported_rates, rates, sizeof(rates[0]) * num_rates);

    auto ht = bss_->Ht();
    if (ht.ready) {
        assoc.has_ht_cap = true;
        HtCapabilities ht_cap = BuildHtCapabilities(ht);

        assoc.ht_cap = ht_cap.ToDdk();
    }

    // TODO(NET-1708): Support VHT MSC

    return assoc;
}

}  // namespace wlan
