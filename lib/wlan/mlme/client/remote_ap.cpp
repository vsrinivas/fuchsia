// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/channel.h>
#include <wlan/common/mac_frame.h>
#include <wlan/mlme/client/remote_ap.h>
#include <wlan/mlme/debug.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>
#include <wlan/mlme/timer.h>

namespace wlan {
namespace remote_ap {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

// RemoteAp implementation.

RemoteAp::RemoteAp(DeviceInterface* device, fbl::unique_ptr<Timer> timer,
                   const wlan_mlme::BSSDescription& bss)
    : device_(device), timer_(fbl::move(timer)) {
    ZX_DEBUG_ASSERT(timer_ != nullptr);
    debugclt("[ap] [%s] spawned\n", bssid_.ToString().c_str());

    bss_ = wlan_mlme::BSSDescription::New();
    bss.Clone(bss_.get());
    bssid_.Set(bss_->bssid.data());
    wlan_channel_t bss_chan{
        .primary = bss_->chan.primary,
        .cbw = static_cast<uint8_t>(bss_->chan.cbw),
    };
    bss_chan_ = SanitizeChannel(bss_chan);

    MoveToState(fbl::make_unique<InitState>(this));
}

RemoteAp::~RemoteAp() {
    // Terminate the current state.
    state_->OnExit();
    state_.reset();

    debugclt("[ap] [%s] destroyed\n", bssid_.ToString().c_str());
}

void RemoteAp::MoveToState(fbl::unique_ptr<BaseState> to) {
    ZX_DEBUG_ASSERT(to != nullptr);
    if (to == nullptr) {
        auto from_name = (state_ == nullptr ? "(init)" : state_->name());
        errorf("attempt to transition to a nullptr from state: %s\n", from_name);
        return;
    }

    if (state_ != nullptr) { state_->OnExit(); }

    debugclt("[ap] [%s] %s -> %s\n", bssid_.ToString().c_str(), state_->name(), to->name());
    state_ = fbl::move(to);
    state_->OnEnter();
}

zx_status_t RemoteAp::StartTimer(zx::time deadline) {
    CancelTimer();
    return timer_->SetTimer(deadline);
}

zx_status_t RemoteAp::CancelTimer() {
    return timer_->CancelTimer();
}

zx::time RemoteAp::CreateTimerDeadline(wlan_tu_t tus) {
    return timer_->Now() + WLAN_TU(tus);
}

bool RemoteAp::IsDeadlineExceeded(zx::time deadline) {
    return deadline > zx::time() && timer_->Now() >= deadline;
}

void RemoteAp::HandleTimeout() {
    state_->HandleTimeout();
}

zx_status_t RemoteAp::HandleMgmtFrame(const MgmtFrameHeader& hdr) {
    // TODO(hahnr): Add stats support.

    // Drop all management frames from other BSS.
    return (bssid_ != hdr.addr3 ? ZX_ERR_STOP : ZX_OK);
}

// TODO(NET-449): Move this logic to policy engine
wlan_channel_t RemoteAp::SanitizeChannel(wlan_channel_t chan) {
    // Validation and sanitization
    if (!common::IsValidChan(chan)) {
        wlan_channel_t chan_sanitized = chan;
        chan_sanitized.cbw = common::GetValidCbw(chan);
        errorf("Wlanstack attempts to configure an invalid channel: %s. Falling back to %s\n",
               common::ChanStr(chan).c_str(), common::ChanStr(chan_sanitized).c_str());
        chan = chan_sanitized;
    }

    // Override with CBW40 support
    if (IsCbw40RxReady()) {
        wlan_channel_t chan_override = chan;
        chan_override.cbw = CBW40;
        chan_override.cbw = common::GetValidCbw(chan_override);

        infof("CBW40 Rx is ready. Overriding the channel configuration from %s to %s\n",
              common::ChanStr(chan).c_str(), common::ChanStr(chan_override).c_str());
        chan = chan_override;
    }
    return chan;
}

bool RemoteAp::IsHTReady() const {
    // TODO(porce): Test capabilites and configurations of the client and its BSS.
    return true;
}

bool RemoteAp::IsCbw40RxReady() const {
    // TODO(porce): Test capabilites and configurations of the client and its BSS.
    return true;
}

bool RemoteAp::IsCbw40TxReady() const {
    // TODO(porce): Test capabilites and configurations of the client and its BSS.
    // TODO(porce): Ralink dependency on BlockAck, AMPDU handling
    return false;
}

bool RemoteAp::IsQosReady() const {
    // TODO(NET-567,NET-599): Determine for each outbound data frame,
    // given the result of the dynamic capability negotiation, data frame
    // classification, and QoS policy.

    // Aruba / Ubiquiti are confirmed to be compatible with QoS field for the BlockAck session,
    // independently of 40MHz operation.
    return true;
}

bool RemoteAp::IsAmsduRxReady() const {
    // [Interop]
    // IEEE Std 802.11-2016 9.4.1.14's wording is ambiguous, and it can cause interop issue.
    // In particular, a peer may tear off BlockAck session if interpretation of the field
    // "A-MSDU Supported" in Block Ack Parameter set of ADDBA Request and Response is different.
    // Declare such that Fuchsia "can do" AMSDU. This hints the peer that
    // peer may assume that this Fuchsia device can process inbound A-MSDU data frame.
    // Since the presence of A-MSDU frame is indicated in the "amsdu_present" field of
    // QoS field in MPDU header, and the use of A-MSDU frame is optional in flight-time,
    // setting "A-MSDU Supported" both in ADDBA Request and Response is deemed to be most
    // interoperable way.
    return false;
}

HtCapabilities RemoteAp::BuildHtCapabilities() {
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
    mcs.tx_mcs.set_set_defined(1);  // Aruba

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

// BaseState implementation.

template <typename S, typename... Args> void RemoteAp::BaseState::MoveToState(Args&&... args) {
    static_assert(fbl::is_base_of<BaseState, S>::value, "Invalid State type");
    ap_->MoveToState(fbl::make_unique<S>(ap_, std::forward<Args>(args)...));
}

// InitState implementation.

InitState::InitState(RemoteAp* ap) : RemoteAp::BaseState(ap) {}

void InitState::OnExit() {
    ap_->CancelTimer();
}

zx_status_t InitState::HandleMlmeJoinReq(const MlmeMsg<wlan_mlme::JoinRequest>& req) {
    debugfn();

    const auto& chan = ap_->bss_chan();
    debugclt("[ap] [%s] setting channel to %s\n", ap_->bssid_str(), common::ChanStr(chan).c_str());
    auto status = ap_->device()->SetChannel(chan);
    if (status != ZX_OK) {
        errorf("[ap] [%s] could not set wlan channel to %s (status %d)\n", ap_->bssid_str(),
               common::ChanStr(chan).c_str(), status);
        service::SendJoinConfirm(ap_->device(), wlan_mlme::JoinResultCodes::JOIN_FAILURE_TIMEOUT);
        return status;
    }

    wlan_tu_t tu = ap_->bss().beacon_period * req.body()->join_failure_timeout;
    join_deadline_ = ap_->CreateTimerDeadline(tu);
    status = ap_->StartTimer(join_deadline_);
    if (status != ZX_OK) {
        errorf("[ap] [%s] could not set join timer: %d\n", ap_->bssid_str(), status);
        service::SendJoinConfirm(ap_->device(), wlan_mlme::JoinResultCodes::JOIN_FAILURE_TIMEOUT);
        return status;
    }

    // TODO(hahnr): Update when other BSS types are supported.
    wlan_bss_config_t cfg{
        .bss_type = WLAN_BSS_TYPE_INFRASTRUCTURE,
        .remote = true,
    };
    ap_->bssid().CopyTo(cfg.bssid);
    ap_->device()->ConfigureBss(&cfg);
    return status;
}

zx_status_t InitState::HandleBeacon(const MgmtFrame<Beacon>& frame) {
    MoveToJoinedState();
    return ZX_OK;
}

zx_status_t InitState::HandleProbeResponse(const MgmtFrame<ProbeResponse>& frame) {
    MoveToJoinedState();
    return ZX_OK;
}

void InitState::MoveToJoinedState() {
    debugfn();

    // Beacon or ProbeResponse received; cancel deadline and move to joined-state.
    join_deadline_ = zx::time();
    service::SendJoinConfirm(ap_->device(), wlan_mlme::JoinResultCodes::SUCCESS);
    MoveToState<JoinedState>();
}

void InitState::HandleTimeout() {
    if (ap_->IsDeadlineExceeded(join_deadline_)) {
        service::SendJoinConfirm(ap_->device(), wlan_mlme::JoinResultCodes::JOIN_FAILURE_TIMEOUT);
    }
}

// JoinedState implementation.

JoinedState::JoinedState(RemoteAp* ap) : RemoteAp::BaseState(ap) {}

zx_status_t JoinedState::HandleMlmeAuthReq(const MlmeMsg<wlan_mlme::AuthenticateRequest>& req) {
    debugfn();

    debugjoin("[ap] [%s] received MLME-AUTHENTICATION.request\n", ap_->bssid_str());

    // TODO(tkilbourn): better result codes
    common::MacAddr peer_sta_addr(req.body()->peer_sta_address.data());
    if (ap_->bssid() != peer_sta_addr) {
        errorf("[ap] [%s] received authentication request for other BSS\n", ap_->bssid_str());
        return service::SendAuthConfirm(ap_->device(), ap_->bssid(),
                                        wlan_mlme::AuthenticateResultCodes::REFUSED);
    }

    if (req.body()->auth_type != wlan_mlme::AuthenticationTypes::OPEN_SYSTEM) {
        // TODO(tkilbourn): support other authentication types
        // TODO(tkilbourn): set the auth_alg_ when we support other authentication types
        errorf("[ap] [%s] only OpenSystem authentication is supported\n", ap_->bssid_str());
        return service::SendAuthConfirm(ap_->device(), ap_->bssid(),
                                        wlan_mlme::AuthenticateResultCodes::REFUSED);
    }

    MgmtFrame<Authentication> frame;
    auto status = BuildMgmtFrame(&frame);
    if (status != ZX_OK) {
        errorf("[ap] [%s] authing: failed to build a frame\n", ap_->bssid_str());
        return service::SendAuthConfirm(ap_->device(), ap_->bssid(),
                                        wlan_mlme::AuthenticateResultCodes::REFUSED);
    }

    auto hdr = frame.hdr();
    const common::MacAddr& client_addr = ap_->device()->GetState()->address();
    hdr->addr1 = ap_->bssid();
    hdr->addr2 = client_addr;
    hdr->addr3 = ap_->bssid();
    SetSeqNo(hdr, ap_->seq());
    frame.FillTxInfo();

    // Only Open System authentication is supported so far.
    auto auth = frame.body();
    auth->auth_algorithm_number = AuthAlgorithm::kOpenSystem;
    auth->auth_txn_seq_number = 1;
    auth->status_code = 0;  // Reserved: set to 0

    finspect("Outbound Mgmt Frame(Auth): %s\n", debug::Describe(*hdr).c_str());
    status = ap_->device()->SendWlan(frame.Take());
    if (status != ZX_OK) {
        errorf("[ap] [%s] could not send auth packet: %d\n", ap_->bssid_str(), status);
        service::SendAuthConfirm(ap_->device(), ap_->bssid(),
                                 wlan_mlme::AuthenticateResultCodes::REFUSED);
        return status;
    }

    MoveToState<AuthenticatingState>(AuthAlgorithm::kOpenSystem, req.body()->auth_failure_timeout);
    return status;
}

// AuthenticatingState implementation.

AuthenticatingState::AuthenticatingState(RemoteAp* ap, AuthAlgorithm auth_alg,
                                         wlan_tu_t auth_timeout_tu)
    : RemoteAp::BaseState(ap), auth_alg_(auth_alg) {
    auth_deadline_ = ap_->CreateTimerDeadline(auth_timeout_tu);
    auto status = ap_->StartTimer(auth_deadline_);
    if (status != ZX_OK) {
        errorf("[ap] [%s] could not set auth timer: %d\n", ap_->bssid_str(), status);

        // This is the wrong result code, but we need to define our own codes at some later time.
        MoveOn<JoinedState>(wlan_mlme::AuthenticateResultCodes::AUTHENTICATION_REJECTED);
    }
}

void AuthenticatingState::OnExit() {
    ap_->CancelTimer();
}

void AuthenticatingState::HandleTimeout() {
    if (ap_->IsDeadlineExceeded(auth_deadline_)) {
        auth_deadline_ = zx::time();
        ap_->CancelTimer();

        MoveOn<JoinedState>(wlan_mlme::AuthenticateResultCodes::AUTH_FAILURE_TIMEOUT);
    }
}

zx_status_t AuthenticatingState::HandleAuthentication(const MgmtFrame<Authentication>& frame) {
    debugfn();

    // Received Authentication response; cancel timeout
    auth_deadline_ = zx::time();
    ap_->CancelTimer();

    auto auth = frame.body();
    if (auth->auth_algorithm_number != auth_alg_) {
        errorf("[ap] [%s] mismatched authentication algorithm (expected %u, got %u)\n",
               ap_->bssid_str(), auth_alg_, auth->auth_algorithm_number);
        MoveOn<JoinedState>(wlan_mlme::AuthenticateResultCodes::AUTHENTICATION_REJECTED);
        return ZX_ERR_BAD_STATE;
    }

    // TODO(tkilbourn): this only makes sense for Open System.
    if (auth->auth_txn_seq_number != 2) {
        errorf("[ap] [%s] unexpected auth txn sequence number (expected 2, got %u)\n",
               ap_->bssid_str(), auth->auth_txn_seq_number);
        MoveOn<JoinedState>(wlan_mlme::AuthenticateResultCodes::AUTHENTICATION_REJECTED);
        return ZX_ERR_BAD_STATE;
    }

    if (auth->status_code != status_code::kSuccess) {
        errorf("[ap] [%s] authentication failed (status code=%u)\n", ap_->bssid_str(),
               auth->status_code);
        // TODO(tkilbourn): is this the right result code?
        MoveOn<JoinedState>(wlan_mlme::AuthenticateResultCodes::AUTHENTICATION_REJECTED);
        return ZX_ERR_BAD_STATE;
    }

    debugjoin("[ap] [%s] authenticated\n", ap_->bssid_str());
    MoveOn<AuthenticatedState>(wlan_mlme::AuthenticateResultCodes::SUCCESS);
    return ZX_OK;
}

template <typename State>
void AuthenticatingState::MoveOn(wlan_mlme::AuthenticateResultCodes result_code) {
    service::SendAuthConfirm(ap_->device(), ap_->bssid(), result_code);
    MoveToState<State>();
}

// AuthenticatedState implementation.

AuthenticatedState::AuthenticatedState(RemoteAp* ap) : RemoteAp::BaseState(ap) {}

zx_status_t AuthenticatedState::HandleMlmeAssocReq(
    const MlmeMsg<wlan_mlme::AssociateRequest>& req) {
    debugfn();

    // TODO(tkilbourn): better result codes
    common::MacAddr peer_sta_addr(req.body()->peer_sta_address.data());
    if (ap_->bssid() != peer_sta_addr) {
        errorf("bad peer STA address for association\n");
        service::SendAssocConfirm(ap_->device(),
                                  wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        return ZX_OK;
    }

    debugjoin("associating to %s\n", ap_->bssid_str());

    size_t body_payload_len = 128;
    MgmtFrame<AssociationRequest> frame;
    auto status = BuildMgmtFrame(&frame, body_payload_len);
    if (status != ZX_OK) {
        service::SendAssocConfirm(ap_->device(),
                                  wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        return ZX_ERR_NO_RESOURCES;
    }

    // TODO(tkilbourn): a lot of this is hardcoded for now. Use device capabilities to set up the
    // request.
    auto hdr = frame.hdr();
    const common::MacAddr& client_addr = ap_->device()->GetState()->address();
    hdr->addr1 = ap_->bssid();
    hdr->addr2 = client_addr;
    hdr->addr3 = ap_->bssid();
    SetSeqNo(hdr, ap_->seq());
    frame.FillTxInfo();

    auto assoc = frame.body();
    assoc->cap.set_ess(1);
    assoc->cap.set_short_preamble(0);  // For robustness. TODO(porce): Enforce Ralink
    assoc->listen_interval = 0;

    ElementWriter w(assoc->elements, frame.body_len() - sizeof(AssociationRequest));
    if (!w.write<SsidElement>(ap_->bss().ssid->data())) {
        errorf("could not write ssid \"%s\" to association request\n", ap_->bss().ssid->data());
        service::SendAssocConfirm(ap_->device(),
                                  wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        return ZX_ERR_IO;
    }

    // TODO(tkilbourn): determine these rates based on hardware and the AP
    std::vector<uint8_t> rates = {0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24};
    if (!w.write<SupportedRatesElement>(fbl::move(rates))) {
        errorf("could not write supported rates\n");
        service::SendAssocConfirm(ap_->device(),
                                  wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        return ZX_ERR_IO;
    }

    std::vector<uint8_t> ext_rates = {0x30, 0x48, 0x60, 0x6c};
    if (!w.write<ExtendedSupportedRatesElement>(fbl::move(ext_rates))) {
        errorf("could not write extended supported rates\n");
        service::SendAssocConfirm(ap_->device(),
                                  wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        return ZX_ERR_IO;
    }

    // Write RSNE from MLME-Association.request if available.
    if (req.body()->rsn) {
        if (!w.write<RsnElement>(req.body()->rsn->data(), req.body()->rsn->size())) {
            errorf("could not write RSNE\n");
            service::SendAssocConfirm(ap_->device(),
                                      wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
            return ZX_ERR_IO;
        }
    }

    if (ap_->IsHTReady()) {
        HtCapabilities htc = ap_->BuildHtCapabilities();
        if (!w.write<HtCapabilities>(htc.ht_cap_info, htc.ampdu_params, htc.mcs_set, htc.ht_ext_cap,
                                     htc.txbf_cap, htc.asel_cap)) {
            errorf("could not write HtCapabilities\n");
            service::SendAssocConfirm(ap_->device(),
                                      wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
            return ZX_ERR_IO;
        }
    }

    // Validate the request in debug mode
    ZX_DEBUG_ASSERT(assoc->Validate(w.size()));

    size_t body_len = sizeof(AssociationRequest) + w.size();
    status = frame.set_body_len(body_len);
    if (status != ZX_OK) {
        errorf("could not set body length to %zu: %d\n", body_len, status);
        service::SendAssocConfirm(ap_->device(),
                                  wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        return status;
    }

    finspect("Outbound Mgmt Frame (AssocReq): %s\n", debug::Describe(*hdr).c_str());
    status = ap_->device()->SendWlan(frame.Take());
    if (status != ZX_OK) {
        errorf("could not send assoc packet: %d\n", status);
        service::SendAssocConfirm(ap_->device(),
                                  wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        return status;
    }

    MoveToState<AssociatingState>();
    return status;
}

// AssociatingState implementation.

AssociatingState::AssociatingState(RemoteAp* ap) : RemoteAp::BaseState(ap) {
    // TODO(tkilbourn): get the assoc timeout from somewhere
    assoc_deadline_ = ap_->CreateTimerDeadline(kAssocTimeoutTu);
    auto status = ap_->StartTimer(assoc_deadline_);
    if (status != ZX_OK) {
        errorf("could not set auth timer: %d\n", status);
        service::SendAssocConfirm(ap_->device(),
                                  wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        MoveToState<AuthenticatedState>();
    }
}

void AssociatingState::OnExit() {
    ap_->CancelTimer();
}

void AssociatingState::HandleTimeout() {
    if (ap_->IsDeadlineExceeded(assoc_deadline_)) {
        assoc_deadline_ = zx::time();
        ap_->CancelTimer();

        service::SendAssocConfirm(ap_->device(),
                                  wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        MoveToState<AuthenticatedState>();
    }
}

zx_status_t AssociatingState::HandleAssociationResponse(
    const MgmtFrame<AssociationResponse>& frame) {
    debugfn();

    // Associations response received; cancel timer and evaluate response.
    assoc_deadline_ = zx::time();
    ap_->CancelTimer();

    auto assoc = frame.body();
    if (assoc->status_code != status_code::kSuccess) {
        errorf("association failed (status code=%u)\n", assoc->status_code);
        // TODO(tkilbourn): map to the correct result code
        service::SendAssocConfirm(ap_->device(),
                                  wlan_mlme::AssociateResultCodes::REFUSED_REASON_UNSPECIFIED);
        MoveToState<AuthenticatedState>();
        return ZX_OK;
    }

    aid_t aid = assoc->aid & kAidMask;
    service::SendAssocConfirm(ap_->device(), wlan_mlme::AssociateResultCodes::SUCCESS);
    MoveToState<AssociatedState>(aid);
    return ZX_OK;
}

// AssociatedState implementation.

AssociatedState::AssociatedState(RemoteAp* ap, aid_t aid) : RemoteAp::BaseState(ap) {
    infof("associated to: %zu\n", aid);

    // TODO(hahnr): Setup link status, signal report and send ADDBA request.
}

}  // namespace remote_ap
}  // namespace wlan
