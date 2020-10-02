// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A state machine for associating a Client to a BSS.
//! Note: This implementation only supports simultaneous authentication with exactly one STA, the
//! AP. While 802.11 explicitly allows - and sometime requires - authentication with more than one
//! STA, Fuchsia does intentionally not yet support this use-case.

use {
    crate::{
        akm_algorithm as akm,
        block_ack::{BlockAckState, Closed},
        client::{lost_bss::LostBssCounter, BoundClient, Client, Context, TimedEvent},
        ddk_converter as ddk,
        disconnect::LocallyInitiated,
        error::Error,
        key::KeyConfig,
        timer::*,
    },
    banjo_ddk_protocol_wlan_mac as banjo_wlan_mac, fidl_fuchsia_wlan_mlme as fidl_mlme,
    fuchsia_zircon as zx,
    log::{error, info, warn},
    static_assertions::assert_eq_size,
    std::convert::TryInto,
    wlan_common::{
        buffer_reader::BufferReader,
        energy::DecibelMilliWatt,
        ie,
        mac::{self, MacAddr, PowerState},
        stats::SignalStrengthAverage,
        tim,
        time::TimeUnit,
    },
    wlan_statemachine::*,
    zerocopy::{AsBytes, ByteSlice},
};

/// Association timeout in Beacon periods.
/// If no association response was received from he BSS within this time window, an association is
/// considered to have failed.
// TODO(fxbug.dev/41609): Let upper layers set this value.
const ASSOC_TIMEOUT_BCN_PERIODS: u16 = 10;

/// Number of beacon intervals which beacon is not seen before we declare BSS as lost
pub const DEFAULT_AUTO_DEAUTH_TIMEOUT_BEACON_COUNT: u32 = 100;

/// Number of beacon intervals between association status check (signal report or auto-deatuh).
pub const ASSOCIATION_STATUS_TIMEOUT_BEACON_COUNT: u32 = 10;

type HtOpByteArray = [u8; fidl_mlme::HT_OP_LEN as usize];
type VhtOpByteArray = [u8; fidl_mlme::VHT_OP_LEN as usize];

/// Client joined a BSS (synchronized timers and prepared its underlying hardware).
/// At this point the Client is able to listen to frames on the BSS' channel.
pub struct Joined;

impl Joined {
    /// Initiates an open authentication with the currently joined BSS.
    /// The returned state is unchanged in an error case. Otherwise, the state transitions into
    /// "Authenticating".
    /// Returns Ok(timeout) if authentication request was sent successfully, Err(()) otherwise.
    fn on_sme_authenticate(
        &self,
        sta: &mut BoundClient<'_>,
        timeout_bcn_count: u16,
        auth_type: fidl_mlme::AuthenticationTypes,
    ) -> Result<akm::AkmAlgorithm<EventId>, ()> {
        let timeout = TimeUnit(sta.sta.beacon_period * timeout_bcn_count);
        let mut algorithm = match auth_type {
            fidl_mlme::AuthenticationTypes::OpenSystem => {
                akm::AkmAlgorithm::open_supplicant(timeout)
            }
            fidl_mlme::AuthenticationTypes::Sae => akm::AkmAlgorithm::sae_supplicant(timeout),
            _ => {
                error!("Unhandled authentication algorithm: {:?}", auth_type);
                return Err(());
            }
        };
        match algorithm.initiate(sta) {
            Ok(akm::AkmState::Failed) | Err(_) => {
                sta.send_authenticate_conf(
                    algorithm.auth_type(),
                    fidl_mlme::AuthenticateResultCodes::Refused,
                );
                error!("Failed to initiate authentication");
                Err(())
            }
            _ => Ok(algorithm),
        }
    }
}

/// Client issued an authentication request frame to its joined BSS prior to joining this state.
/// At this point the client is waiting for an authentication response frame from the client.
/// Note: This assumes Open System authentication.
pub struct Authenticating {
    algorithm: akm::AkmAlgorithm<EventId>,
}

impl Authenticating {
    fn akm_state_update_notify_sme(
        &self,
        sta: &mut BoundClient<'_>,
        state: Result<akm::AkmState, anyhow::Error>,
    ) -> akm::AkmState {
        match state {
            Ok(akm::AkmState::AuthComplete) => {
                sta.send_authenticate_conf(
                    self.algorithm.auth_type(),
                    fidl_mlme::AuthenticateResultCodes::Success,
                );
                akm::AkmState::AuthComplete
            }
            Ok(akm::AkmState::InProgress) => akm::AkmState::InProgress,
            Ok(akm::AkmState::Failed) => {
                error!("authentication with BSS failed");
                sta.send_authenticate_conf(
                    self.algorithm.auth_type(),
                    fidl_mlme::AuthenticateResultCodes::AuthenticationRejected,
                );
                akm::AkmState::Failed
            }
            Err(e) => {
                error!("Internal error while authenticating: {}", e);
                sta.send_authenticate_conf(
                    self.algorithm.auth_type(),
                    fidl_mlme::AuthenticateResultCodes::AuthenticationRejected,
                );
                akm::AkmState::Failed
            }
        }
    }

    /// Processes an inbound authentication frame.
    /// SME will be notified via an MLME-AUTHENTICATE.confirm message whether the authentication
    /// with the BSS was successful.
    /// Returns Ok(()) if the authentication was successful, otherwise Err(()).
    /// Note: The pending authentication timeout will be canceled in any case.
    fn on_auth_frame(
        &mut self,
        sta: &mut BoundClient<'_>,
        auth_hdr: &mac::AuthHdr,
        body: &[u8],
    ) -> akm::AkmState {
        let state = self.algorithm.handle_auth_frame(sta, auth_hdr, Some(body));
        self.akm_state_update_notify_sme(sta, state)
    }

    /// Processes an SAE response from SME.
    /// This indicates that an SAE handshake has completed, successful or otherwise.
    /// On success, authentication is complete.
    fn on_sme_sae_resp(
        &mut self,
        sta: &mut BoundClient<'_>,
        resp: fidl_mlme::SaeHandshakeResponse,
    ) -> akm::AkmState {
        let state = self.algorithm.handle_sae_resp(sta, resp.result_code);
        self.akm_state_update_notify_sme(sta, state)
    }

    /// Processes a request from SME to transmit an SAE authentication frame to a peer.
    fn on_sme_sae_tx(
        &mut self,
        sta: &mut BoundClient<'_>,
        tx: fidl_mlme::SaeFrame,
    ) -> akm::AkmState {
        let state =
            self.algorithm.handle_sme_sae_tx(sta, tx.seq_num, tx.result_code, &tx.sae_fields[..]);
        self.akm_state_update_notify_sme(sta, state)
    }

    /// Processes an inbound deauthentication frame.
    /// This always results in an MLME-AUTHENTICATE.confirm message to MLME's SME peer.
    /// The pending authentication timeout will be canceled in this process.
    fn on_deauth_frame(&mut self, sta: &mut BoundClient<'_>, deauth_hdr: &mac::DeauthHdr) {
        info!(
            "received spurious deauthentication frame while authenticating with BSS (unusual); \
             authentication failed: {:?}",
            { deauth_hdr.reason_code }
        );

        self.algorithm.cancel(sta);
        sta.send_authenticate_conf(
            self.algorithm.auth_type(),
            fidl_mlme::AuthenticateResultCodes::Refused,
        );
    }

    /// Invoked when the pending timeout fired. The original authentication request is now
    /// considered to be expired and invalid - the authentication failed. As a consequence,
    /// an MLME-AUTHENTICATION.confirm message is reported to MLME's SME peer indicating the
    /// timeout.
    fn on_timeout(&mut self, sta: &mut BoundClient<'_>, event: EventId) {
        // Timeout may result in a failure, and otherwise has no effect on state.
        match self.algorithm.handle_timeout(sta, event) {
            Ok(akm::AkmState::Failed) => {
                sta.send_authenticate_conf(
                    self.algorithm.auth_type(),
                    fidl_mlme::AuthenticateResultCodes::AuthFailureTimeout,
                );
            }
            _ => (),
        }
    }

    fn on_sme_deauthenticate(&mut self, sta: &mut BoundClient<'_>) {
        self.algorithm.cancel(sta);
    }
}

/// Client received a "successful" authentication response from the BSS.
pub struct Authenticated;

impl Authenticated {
    /// Initiates an association with the currently joined BSS.
    /// Returns Ok(timeout) if association request was sent successfully.
    /// Otherwise an Err(()) is returned and an ASSOCIATE.confirm message to its SME peer.
    fn on_sme_associate(
        &self,
        sta: &mut BoundClient<'_>,
        req: fidl_mlme::AssociateRequest,
    ) -> Result<EventId, ()> {
        // clone ssid here because we will mutably borrow sta.
        let cap_info = req.cap_info;
        let rsne = req.rsne.as_ref().map(|rsne| &rsne[..]).unwrap_or(&[]);
        let ht_cap = req.ht_cap.as_ref().map(|h| &h.bytes[..]).unwrap_or(&[]);
        let vht_cap = req.vht_cap.as_ref().map(|v| &v.bytes[..]).unwrap_or(&[]);

        match sta.send_assoc_req_frame(cap_info, &req.rates[..], rsne, ht_cap, vht_cap) {
            Ok(()) => {
                let duration_tus = TimeUnit(sta.sta.beacon_period) * ASSOC_TIMEOUT_BCN_PERIODS;
                let deadline = sta.ctx.timer.now() + duration_tus.into();
                let event = TimedEvent::Associating;
                let event_id = sta.ctx.timer.schedule_event(deadline, event);
                Ok(event_id)
            }
            Err(e) => {
                error!("Error sending association request frame: {}", e);
                sta.send_associate_conf_failure(
                    fidl_mlme::AssociateResultCodes::RefusedTemporarily,
                );
                Err(())
            }
        }
    }

    /// Sends an MLME-DEAUTHENTICATE.indication message to MLME's SME peer.
    fn on_deauth_frame(&self, sta: &mut BoundClient<'_>, deauth_hdr: &mac::DeauthHdr) {
        let reason_code = fidl_mlme::ReasonCode::from_primitive(deauth_hdr.reason_code.0)
            .unwrap_or(fidl_mlme::ReasonCode::UnspecifiedReason);
        sta.send_deauthenticate_ind(reason_code, LocallyInitiated(false));
    }

    fn on_sme_deauthenticate(
        &self,
        sta: &mut BoundClient<'_>,
        req: fidl_mlme::DeauthenticateRequest,
    ) {
        if let Err(e) = sta.send_deauth_frame(mac::ReasonCode(req.reason_code.into_primitive())) {
            error!("Error sending deauthentication frame to BSS: {}", e);
        }

        if let Err(e) = sta.ctx.device.access_sme_sender(|sender| {
            sender.send_deauthenticate_conf(&mut fidl_mlme::DeauthenticateConfirm {
                peer_sta_address: sta.sta.bssid.0,
            })
        }) {
            error!("Error sending MLME-DEAUTHENTICATE.confirm: {}", e)
        }
    }
}

/// Client received an MLME-ASSOCIATE.request message from SME.
pub struct Associating {
    timeout: EventId,
}

impl Associating {
    /// Processes an inbound association response frame.
    /// SME will be notified via an MLME-ASSOCIATE.confirm message whether the association
    /// with the BSS was successful.
    /// Returns Ok(()) if the association was successful, otherwise Err(()).
    /// Note: The pending authentication timeout will be canceled in any case.
    fn on_assoc_resp_frame<B: ByteSlice>(
        &self,
        sta: &mut BoundClient<'_>,
        assoc_resp_hdr: &mac::AssocRespHdr,
        elements: B,
    ) -> Result<Association, ()> {
        sta.ctx.timer.cancel_event(self.timeout);

        match assoc_resp_hdr.status_code {
            mac::StatusCode::SUCCESS => {
                sta.send_associate_conf_success(
                    assoc_resp_hdr.aid,
                    assoc_resp_hdr.capabilities,
                    &elements[..],
                );
                let (ap_ht_op, ap_vht_op) = extract_ht_vht_op(elements);
                let controlled_port_open = !sta.sta.eapol_required;
                if controlled_port_open {
                    if let Err(e) = sta.ctx.device.set_eth_link_up() {
                        error!("Cannot set ethernet to UP. Status: {}", e);
                    }
                }
                let lost_bss_counter = LostBssCounter::start(
                    sta.sta.beacon_period,
                    DEFAULT_AUTO_DEAUTH_TIMEOUT_BEACON_COUNT,
                );

                let status_check_timeout =
                    schedule_association_status_timeout(sta.sta.beacon_period, &mut sta.ctx.timer);

                Ok(Association {
                    aid: assoc_resp_hdr.aid,
                    controlled_port_open,
                    ap_ht_op,
                    ap_vht_op,
                    qos: Qos::PendingNegotiation,
                    lost_bss_counter,
                    status_check_timeout,
                    signal_strength_average: SignalStrengthAverage::new(),
                    block_ack_state: StateMachine::new(BlockAckState::from(State::new(Closed))),
                })
            }
            status_code => {
                error!("association with BSS failed: {:?}", status_code);
                sta.send_associate_conf_failure(
                    fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified,
                );
                Err(())
            }
        }
    }

    /// Processes an inbound disassociation frame.
    /// Note: APs should never send disassociation frames without having established a valid
    /// association with the Client. However, to maximize interoperability disassociation frames
    /// are handled in this state as well and treated similar to unsuccessful association responses.
    /// This always results in an MLME-ASSOCIATE.confirm message to MLME's SME peer.
    fn on_disassoc_frame(&self, sta: &mut BoundClient<'_>, _disassoc_hdr: &mac::DisassocHdr) {
        sta.ctx.timer.cancel_event(self.timeout);

        warn!("received unexpected disassociation frame while associating");
        sta.send_associate_conf_failure(fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified);
    }

    /// Processes an inbound deauthentication frame.
    /// This always results in an MLME-ASSOCIATE.confirm message to MLME's SME peer.
    /// The pending association timeout will be canceled in this process.
    fn on_deauth_frame(&self, sta: &mut BoundClient<'_>, deauth_hdr: &mac::DeauthHdr) {
        sta.ctx.timer.cancel_event(self.timeout);

        info!(
            "received spurious deauthentication frame while associating with BSS (unusual); \
             association failed: {:?}",
            { deauth_hdr.reason_code }
        );
        sta.send_associate_conf_failure(fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified);
    }

    /// Invoked when the pending timeout fired. The original association request is now
    /// considered to be expired and invalid - the association failed. As a consequence,
    /// an MLME-ASSOCIATE.confirm message is reported to MLME's SME peer indicating the
    /// timeout.
    fn on_timeout(&self, sta: &mut BoundClient<'_>) {
        // At this point, the event should already be canceled by the state's owner. However,
        // ensure the timeout is canceled in any case.
        sta.ctx.timer.cancel_event(self.timeout);

        sta.send_associate_conf_failure(fidl_mlme::AssociateResultCodes::RefusedTemporarily);
    }

    fn on_sme_deauthenticate(&self, sta: &mut BoundClient<'_>) {
        sta.ctx.timer.cancel_event(self.timeout);
    }
}

/// Extract HT Operation and VHT Operation IEs from the association response frame.
/// If either IE is of an incorrect length, it will be ignored.
fn extract_ht_vht_op<B: ByteSlice>(elements: B) -> (Option<HtOpByteArray>, Option<VhtOpByteArray>) {
    // Note the assert_eq_size!() that guarantees these structs match at compile time.
    let mut ht_op: Option<HtOpByteArray> = None;
    let mut vht_op: Option<VhtOpByteArray> = None;
    for (id, body) in ie::Reader::new(elements) {
        match id {
            ie::Id::HT_OPERATION => match ie::parse_ht_operation(body) {
                Ok(h) => {
                    assert_eq_size!(ie::HtOperation, HtOpByteArray);
                    ht_op = Some(h.as_bytes().try_into().unwrap());
                }
                Err(e) => {
                    error!("Invalid HT Operation: {}", e);
                    continue;
                }
            },
            ie::Id::VHT_OPERATION => match ie::parse_vht_operation(body) {
                Ok(v) => {
                    assert_eq_size!(ie::VhtOperation, VhtOpByteArray);
                    vht_op = Some(v.as_bytes().try_into().unwrap());
                }
                Err(e) => {
                    error!("Invalid VHT Operation: {}", e);
                    continue;
                }
            },
            _ => (),
        }
    }
    (ht_op, vht_op)
}

pub fn schedule_association_status_timeout(
    beacon_period: u16,
    timer: &mut Timer<TimedEvent>,
) -> StatusCheckTimeout {
    let last_fired = timer.now();
    let deadline = last_fired
        + zx::Duration::from(TimeUnit(beacon_period)) * ASSOCIATION_STATUS_TIMEOUT_BEACON_COUNT;
    StatusCheckTimeout {
        last_fired,
        next_id: timer.schedule_event(deadline, TimedEvent::AssociationStatusCheck),
    }
}

#[derive(Debug, PartialEq)]
pub enum Qos {
    Enabled,
    Disabled,
    // Intermediate state between when an association response frame is received from AP and
    // when a finalize_association is received from SME.
    PendingNegotiation,
}

impl From<bool> for Qos {
    fn from(b: bool) -> Self {
        if b {
            Self::Enabled
        } else {
            Self::Disabled
        }
    }
}

impl Qos {
    fn is_enabled(&self) -> bool {
        *self == Self::Enabled
    }
}

#[derive(Debug)]
pub struct StatusCheckTimeout {
    last_fired: zx::Time,
    next_id: EventId,
}

#[derive(Debug)]
pub struct Association {
    pub aid: mac::Aid,

    /// Represents an 802.1X controlled port.
    /// A closed controlled port only processes EAP frames while an open one processes any frames.
    pub controlled_port_open: bool,

    pub ap_ht_op: Option<HtOpByteArray>,
    pub ap_vht_op: Option<VhtOpByteArray>,

    /// Whether to set QoS bit when MLME constructs an outgoing WLAN data frame.
    /// Currently, QoS is enabled if the associated PHY is HT or VHT.
    pub qos: Qos,

    /// `lost_bss_counter` is used to determine if the BSS is still alive nearby. It is started
    /// when the client is associated.
    pub lost_bss_counter: LostBssCounter,

    /// |timeout| is the timeout that is scheduled for the association status check, which includes
    /// a) sending signal strength report to SME and b) triggering auto-deauth if necessary.
    /// It will be cancelled when the client go off-channel for scanning and scheduled again when
    /// back on channel.
    pub status_check_timeout: StatusCheckTimeout,
    pub signal_strength_average: SignalStrengthAverage,

    pub block_ack_state: StateMachine<BlockAckState>,
}

/// Client received a "successful" association response from the BSS.
pub struct Associated(pub Association);

impl Associated {
    /// Processes an inbound diassociation frame.
    /// This always results in an MLME-DISASSOCIATE.indication message to MLME's SME peer.
    fn on_disassoc_frame(&mut self, sta: &mut BoundClient<'_>, disassoc_hdr: &mac::DisassocHdr) {
        self.pre_leaving_associated_state(sta);
        let reason_code = fidl_mlme::ReasonCode::from_primitive(disassoc_hdr.reason_code.0)
            .unwrap_or(fidl_mlme::ReasonCode::UnspecifiedReason);
        sta.send_disassoc_ind(reason_code, LocallyInitiated(false));
    }

    /// Sends an MLME-DEAUTHENTICATE.indication message to MLME's SME peer.
    fn on_deauth_frame(&mut self, sta: &mut BoundClient<'_>, deauth_hdr: &mac::DeauthHdr) {
        self.pre_leaving_associated_state(sta);
        let reason_code = fidl_mlme::ReasonCode::from_primitive(deauth_hdr.reason_code.0)
            .unwrap_or(fidl_mlme::ReasonCode::UnspecifiedReason);
        sta.send_deauthenticate_ind(reason_code, LocallyInitiated(false));
    }

    /// Process every inbound management frame before its being handed off to a more specific
    /// handler.
    fn on_any_mgmt_frame(&self, sta: &mut BoundClient<'_>, mgmt_hdr: &mac::MgmtHdr) {
        self.request_bu_if_available(sta, mgmt_hdr.frame_ctrl, mgmt_hdr.addr1);
    }

    /// Sends PS-POLL requests if the FrameControl's more_data bit is set, and the received frame
    /// was addressed for this STA. No-op if the controlled port is closed.
    fn request_bu_if_available(
        &self,
        sta: &mut BoundClient<'_>,
        fc: mac::FrameControl,
        dst_addr: MacAddr,
    ) {
        if !self.0.controlled_port_open {
            return;
        }
        // IEEE Std. 802.11-2016, 9.2.4.1.8
        if fc.more_data() && dst_addr == sta.sta.iface_mac {
            let _result = sta.send_ps_poll_frame(self.0.aid);
        }
    }

    fn extract_and_record_signal_dbm(&mut self, rx_info: Option<banjo_wlan_mac::WlanRxInfo>) {
        let rssi_dbm = match rx_info.and_then(|rx_info| ddk::get_rssi_dbm(rx_info)) {
            Some(dbm) => dbm,
            None => return,
        };
        self.0.signal_strength_average.add(DecibelMilliWatt(rssi_dbm));
    }

    /// Process and inbound beacon frame.
    /// Resets LostBssCounter, check buffered frame if available.
    fn on_beacon_frame<B: ByteSlice>(&mut self, sta: &mut BoundClient<'_>, elements: B) {
        self.0.lost_bss_counter.reset();
        for (id, body) in ie::Reader::new(elements) {
            match id {
                ie::Id::TIM => match ie::parse_tim(body) {
                    Ok(ie::TimView { header, bitmap }) => {
                        if tim::is_traffic_buffered(header.bmp_ctrl.offset(), &bitmap, self.0.aid) {
                            let _result = sta.send_ps_poll_frame(self.0.aid);
                        }
                    }
                    _ => (),
                },

                _ => (),
            }
        }
    }

    /// Extracts aggregated and non-aggregated MSDUs from the data frame.
    /// Handles all data subtypes.
    /// EAPoL MSDUs are forwarded to SME via an MLME-EAPOL.indication message independent of the
    /// STA's current controlled port status.
    /// All other MSDUs are converted into Ethernet II frames and forwarded via the device to
    /// Fuchsia's Netstack if the STA's controlled port is open.
    /// NULL-Data frames are interpreted as "Keep Alive" requests and responded with NULL data
    /// frames if the STA's controlled port is open.
    fn on_data_frame<B: ByteSlice>(
        &self,
        sta: &mut BoundClient<'_>,
        fixed_data_fields: &mac::FixedDataHdrFields,
        addr4: Option<mac::Addr4>,
        qos_ctrl: Option<mac::QosControl>,
        body: B,
    ) {
        self.request_bu_if_available(
            sta,
            fixed_data_fields.frame_ctrl,
            mac::data_dst_addr(&fixed_data_fields),
        );

        let msdus =
            mac::MsduIterator::from_data_frame_parts(*fixed_data_fields, addr4, qos_ctrl, body);

        // Handle NULL data frames independent of the controlled port's status.
        if let mac::MsduIterator::Null = msdus {
            if let Err(e) = sta.send_keep_alive_resp_frame() {
                error!("error sending keep alive frame: {}", e);
            }
        }
        // Handle aggregated and non-aggregated MSDUs.
        for msdu in msdus {
            let mac::Msdu { dst_addr, src_addr, llc_frame } = &msdu;
            match llc_frame.hdr.protocol_id.to_native() {
                // Forward EAPoL frames to SME independent of the controlled port's
                // status.
                mac::ETHER_TYPE_EAPOL => {
                    if let Err(e) =
                        sta.send_eapol_indication(*src_addr, *dst_addr, &llc_frame.body[..])
                    {
                        error!("error sending MLME-EAPOL.indication: {}", e);
                    }
                }
                // Deliver non-EAPoL MSDUs only if the controlled port is open.
                _ if self.0.controlled_port_open => {
                    if let Err(e) = sta.deliver_msdu(msdu) {
                        error!("error while handling data frame: {}", e);
                    }
                }
                // Drop all non-EAPoL MSDUs if the controlled port is closed.
                _ => (),
            }
        }
    }

    fn on_eth_frame<B: ByteSlice>(&self, sta: &mut BoundClient<'_>, frame: B) -> Result<(), Error> {
        let mac::EthernetFrame { hdr, body } = match mac::EthernetFrame::parse(frame) {
            Some(eth_frame) => eth_frame,
            None => {
                return Err(Error::Status(
                    format!("Ethernet frame too short"),
                    zx::Status::IO_DATA_INTEGRITY,
                ));
            }
        };

        if !self.0.controlled_port_open {
            return Err(Error::Status(
                format!("Ethernet dropped. RSN not established"),
                zx::Status::BAD_STATE,
            ));
        }

        sta.send_data_frame(
            hdr.sa,
            hdr.da,
            sta.sta.eapol_required,
            self.0.qos.is_enabled(),
            hdr.ether_type.to_native(),
            &body,
        )
    }

    fn on_block_ack_frame<B: ByteSlice>(
        &mut self,
        sta: &mut BoundClient<'_>,
        action: mac::BlockAckAction,
        body: B,
    ) {
        self.0.block_ack_state.replace_state(|state| state.on_block_ack_frame(sta, action, body));
    }

    /// Process an inbound MLME-FinalizeAssociation.request.
    /// Derive an `AssociationContext` fromthe negotiated capabilities and
    /// install the context in the underlying driver.
    fn on_sme_finalize_association(
        &mut self,
        sta: &mut BoundClient<'_>,
        cap: fidl_mlme::NegotiatedCapabilities,
    ) {
        let Association { aid, ap_ht_op, ap_vht_op, .. } = self.0;

        let assoc_ctx = ddk::build_ddk_assoc_ctx(sta.sta.bssid, aid, cap, ap_ht_op, ap_vht_op);
        // TODO(fxbug.dev/29325): Determine for each outbound data frame,
        // given the result of the dynamic capability negotiation, data frame
        // classification, and QoS policy.
        //
        // Aruba / Ubiquiti are confirmed to be compatible with QoS field for the
        // BlockAck session, independently of 40MHz operation.
        self.0.qos = assoc_ctx.qos.into();

        if let Err(status) = sta.ctx.device.configure_assoc(assoc_ctx) {
            // Device cannot handle this association. Something is seriously wrong.
            // TODO(eyw): Is this allowed? Should we deauthenticate or panic instead?
            error!("device failed to configure association: {}", status);
        }
    }

    fn on_sme_eapol(&self, sta: &mut BoundClient<'_>, req: fidl_mlme::EapolRequest) {
        // Drop EAPoL frame if it is not a protected network.
        if !sta.sta.eapol_required {
            error!("Unexpected MLME-EAPOL.request message: BSS not protected");
            return;
        }
        // There may be more EAPoL frames (such as key rotation) coming after EAPoL established.
        // They need to be protected.
        let protected = sta.sta.eapol_required && self.0.controlled_port_open;
        sta.send_eapol_frame(req.src_addr, req.dst_addr, protected, &req.data);
    }

    fn on_sme_set_keys(&self, sta: &BoundClient<'_>, req: fidl_mlme::SetKeysRequest) {
        if !sta.sta.eapol_required {
            error!("Unexpected MLME-SetKeys.request message: BSS not protected");
            return;
        }
        for key_desc in req.keylist {
            if let Err(e) = sta.ctx.device.set_key(KeyConfig::from(&key_desc)) {
                error!("failed to set keys in driver: {}", e);
            }
        }
    }

    fn on_sme_set_controlled_port(
        &mut self,
        sta: &BoundClient<'_>,
        req: fidl_mlme::SetControlledPortRequest,
    ) {
        if !sta.sta.eapol_required {
            error!("Unexpected MLME-SetControlledPort.request message: BSS not protected.");
            return;
        }
        let should_open_controlled_port = req.state == fidl_mlme::ControlledPortState::Open;
        if should_open_controlled_port == self.0.controlled_port_open {
            return;
        }
        self.0.controlled_port_open = should_open_controlled_port;
        if let Err(e) = sta.ctx.device.set_eth_link(req.state.into()) {
            error!(
                "Error settting Ethernet port to {}: {}",
                if should_open_controlled_port { "OPEN" } else { "CLOSED" },
                e
            );
        }
    }

    fn on_sme_deauthenticate(
        &mut self,
        sta: &mut BoundClient<'_>,
        req: fidl_mlme::DeauthenticateRequest,
    ) {
        if let Err(e) = sta.send_deauth_frame(mac::ReasonCode(req.reason_code.into_primitive())) {
            error!("Error sending deauthentication frame to BSS: {}", e);
        }

        self.pre_leaving_associated_state(sta);

        if let Err(e) = sta.ctx.device.access_sme_sender(|sender| {
            sender.send_deauthenticate_conf(&mut fidl_mlme::DeauthenticateConfirm {
                peer_sta_address: sta.sta.bssid.0,
            })
        }) {
            error!("Error sending MLME-DEAUTHENTICATE.confirm: {}", e)
        }
    }

    fn pre_leaving_associated_state(&mut self, sta: &mut BoundClient<'_>) {
        sta.ctx.timer.cancel_event(self.0.status_check_timeout.next_id);
        self.0.controlled_port_open = false;
        if let Err(e) = sta.ctx.device.set_eth_link_down() {
            error!("Error disabling ethernet device offline: {}", e);
        }
        if let Err(e) = sta.ctx.device.clear_assoc(&sta.sta.bssid.0) {
            error!("Error clearing association in vendor drvier: {}", e);
        }
    }

    #[must_use]
    /// Reports average signal strength to SME and check if auto deauthentication is due.
    /// Returns true if there auto deauthentication is triggered by lack of beacon frames.
    fn on_timeout(&mut self, sta: &mut BoundClient<'_>) -> bool {
        // timeout should have been cancelled at this point, this is almost always a no-op.
        sta.ctx.timer.cancel_event(self.0.status_check_timeout.next_id);
        if let Err(e) = sta.ctx.device.access_sme_sender(|sender| {
            sender.send_signal_report(&mut fidl_mlme::SignalReportIndication {
                rssi_dbm: self.0.signal_strength_average.avg_dbm().0,
                snr_db: 0,
            })
        }) {
            error!("Error sending MLME-SignalReport: {}", e)
        }

        let auto_deauth = self.0.lost_bss_counter.should_deauthenticate();
        if auto_deauth {
            sta.send_deauthenticate_ind(
                fidl_mlme::ReasonCode::LeavingNetworkDeauth,
                LocallyInitiated(true),
            );
            if let Err(e) = sta.send_deauth_frame(mac::ReasonCode::LEAVING_NETWORK_DEAUTH) {
                warn!("Failed sending deauth frame {:?}", e);
            }
            self.pre_leaving_associated_state(sta);
        } else {
            // Always check should_deauthenticate() first since even if Client receives a beacon,
            // it would still add a full association status check interval to the lost BSS counter.
            self.0.lost_bss_counter.add_beacon_interval(ASSOCIATION_STATUS_TIMEOUT_BEACON_COUNT);
            self.0.status_check_timeout =
                schedule_association_status_timeout(sta.sta.beacon_period, &mut sta.ctx.timer);
        }
        auto_deauth
    }

    fn off_channel(&mut self, sta: &mut Client, ctx: &mut Context) {
        if let Err(e) = sta.send_power_state_frame(ctx, PowerState::DOZE) {
            warn!("unable to send doze frame: {:?}", e);
        }
        self.0.lost_bss_counter.add_time(ctx.timer.now() - self.0.status_check_timeout.last_fired);
        ctx.timer.cancel_event(self.0.status_check_timeout.next_id);
    }

    fn on_channel(&mut self, sta: &mut Client, ctx: &mut Context) {
        if let Err(e) = sta.send_power_state_frame(ctx, PowerState::AWAKE) {
            warn!("unable to send awake frame: {:?}", e);
        }
        self.0.status_check_timeout =
            schedule_association_status_timeout(sta.beacon_period, &mut ctx.timer);
    }
}

statemachine!(
    /// Client state machine.
    pub enum States,
    // Regular successful flow:
    () => Joined,
    Joined => Authenticating,
    Authenticating => Authenticated,
    Authenticated => Associating,
    Associating => Associated,

    // Timeout:
    Authenticating => Joined,
    Associating => Authenticated,

    // Deauthentication:
    Authenticating => Joined,
    Authenticated => Joined,
    Associating => Joined,
    Associated => Joined,

    // Disassociation:
    Associating => Authenticated,
    Associated => Authenticated,
);

impl States {
    /// Returns the STA's initial state.
    pub fn new_initial() -> States {
        States::from(State::new(Joined))
    }

    /// Callback to process arbitrary IEEE 802.11 frames.
    /// Frames are dropped if:
    /// - frames are corrupted (too short)
    /// - frames' frame class is not yet permitted
    /// - frames are from a foreign BSS
    /// - frames are unicast but destined for a MAC address that is different from this STA.
    // TODO(fxbug.dev/43456): Implement a packet counter and add tests to verify frames are dropped correctly.
    pub fn on_mac_frame<B: ByteSlice>(
        mut self,
        sta: &mut BoundClient<'_>,
        bytes: B,
        rx_info: Option<banjo_wlan_mac::WlanRxInfo>,
    ) -> States {
        // For now, silently drop all frames when we are off channel.
        if !sta.is_on_channel() {
            return self;
        }

        let body_aligned = rx_info.map_or(false, |ri| {
            (ri.rx_flags & banjo_wlan_mac::WlanRxInfoFlags::FRAME_BODY_PADDING_4.0) != 0
        });

        // Parse mac frame. Drop corrupted ones.
        let mac_frame = match mac::MacFrame::parse(bytes, body_aligned) {
            Some(mac_frame) => mac_frame,
            None => return self,
        };

        if !sta.sta.should_handle_frame(&mac_frame) {
            // While scanning, it is normal to receive mac frames from other BSS. Off-channel frames
            // are already dropped at this point. No need to print error messages in this case.
            if !sta.scanner.is_scanning() {
                warn!("Mac frame is either from a foreign BSS or not destined for us. Dropped.");
            }
            return self;
        }

        // Drop frames which are not permitted in the STA's current state.
        let frame_class = mac::FrameClass::from(&mac_frame);
        if !self.is_frame_class_permitted(frame_class) {
            return self;
        }

        match mac_frame {
            mac::MacFrame::Mgmt { mgmt_hdr, body, .. } => {
                self.on_mgmt_frame(sta, &mgmt_hdr, body, rx_info)
            }
            mac::MacFrame::Data { fixed_fields, addr4, qos_ctrl, body, .. } => {
                if let States::Associated(state) = &mut self {
                    state.on_data_frame(
                        sta,
                        &fixed_fields,
                        addr4.map(|x| *x),
                        qos_ctrl.map(|x| x.get()),
                        body,
                    );
                    state.extract_and_record_signal_dbm(rx_info);
                }

                // Drop data frames in all other states
                self
            }
            // Control frames are not yet supported. Drop them.
            _ => self,
        }
    }

    /// Processes inbound management frames.
    /// Only frames from the joined BSS are processed. Frames from other STAs are dropped.
    fn on_mgmt_frame<B: ByteSlice>(
        self,
        sta: &mut BoundClient<'_>,
        mgmt_hdr: &mac::MgmtHdr,
        body: B,
        rx_info: Option<banjo_wlan_mac::WlanRxInfo>,
    ) -> States {
        // Parse management frame. Drop corrupted ones.
        let mgmt_body = match mac::MgmtBody::parse({ mgmt_hdr.frame_ctrl }.mgmt_subtype(), body) {
            Some(x) => x,
            None => return self,
        };

        match self {
            States::Authenticating(mut state) => match mgmt_body {
                mac::MgmtBody::Authentication { auth_hdr, elements } => {
                    match state.on_auth_frame(sta, &auth_hdr, &elements[..]) {
                        akm::AkmState::InProgress => state.into(),
                        akm::AkmState::Failed => state.transition_to(Joined).into(),
                        akm::AkmState::AuthComplete => state.transition_to(Authenticated).into(),
                    }
                }
                mac::MgmtBody::Deauthentication { deauth_hdr, .. } => {
                    state.on_deauth_frame(sta, &deauth_hdr);
                    state.transition_to(Joined).into()
                }
                _ => state.into(),
            },
            States::Authenticated(state) => match mgmt_body {
                mac::MgmtBody::Deauthentication { deauth_hdr, .. } => {
                    state.on_deauth_frame(sta, &deauth_hdr);
                    state.transition_to(Joined).into()
                }
                _ => state.into(),
            },
            States::Associating(state) => match mgmt_body {
                mac::MgmtBody::AssociationResp { assoc_resp_hdr, elements } => {
                    match state.on_assoc_resp_frame(sta, &assoc_resp_hdr, elements) {
                        Ok(association) => state.transition_to(Associated(association)).into(),
                        Err(()) => state.transition_to(Authenticated).into(),
                    }
                }
                mac::MgmtBody::Deauthentication { deauth_hdr, .. } => {
                    state.on_deauth_frame(sta, &deauth_hdr);
                    state.transition_to(Joined).into()
                }
                // This case is highly unlikely and only added to improve interoperability with
                // buggy Access Points.
                mac::MgmtBody::Disassociation { disassoc_hdr, .. } => {
                    state.on_disassoc_frame(sta, &disassoc_hdr);
                    state.transition_to(Authenticated).into()
                }
                _ => state.into(),
            },
            States::Associated(mut state) => {
                state.extract_and_record_signal_dbm(rx_info);
                state.on_any_mgmt_frame(sta, mgmt_hdr);
                match mgmt_body {
                    mac::MgmtBody::Beacon { bcn_hdr: _, elements } => {
                        state.on_beacon_frame(sta, elements);
                        state.into()
                    }
                    mac::MgmtBody::Deauthentication { deauth_hdr, .. } => {
                        state.on_deauth_frame(sta, &deauth_hdr);
                        state.transition_to(Joined).into()
                    }
                    mac::MgmtBody::Disassociation { disassoc_hdr, .. } => {
                        state.on_disassoc_frame(sta, &disassoc_hdr);
                        state.transition_to(Authenticated).into()
                    }
                    mac::MgmtBody::Action { action_hdr, elements, .. } => match action_hdr.action {
                        mac::ActionCategory::BLOCK_ACK => {
                            let reader = BufferReader::new(elements);
                            if let Some(action) = reader.peek_unaligned::<mac::BlockAckAction>() {
                                state.on_block_ack_frame(
                                    sta,
                                    action.get(),
                                    reader.into_remaining(),
                                );
                            }
                            state.into()
                        }
                        _ => state.into(),
                    },
                    _ => state.into(),
                }
            }
            _ => self,
        }
    }

    pub fn on_eth_frame<B: ByteSlice>(
        &self,
        sta: &mut BoundClient<'_>,
        frame: B,
    ) -> Result<(), Error> {
        match self {
            States::Associated(state) if sta.is_on_channel() => state.on_eth_frame(sta, frame),
            States::Associated(_state) => Err(Error::Status(
                format!("Associated but not on main channel. Ethernet dropped"),
                zx::Status::BAD_STATE,
            )),
            _ => Err(Error::Status(
                format!("Not associated. Ethernet dropped"),
                zx::Status::BAD_STATE,
            )),
        }
    }

    /// Callback when a previously scheduled event fired.
    pub fn on_timed_event(
        self,
        sta: &mut BoundClient<'_>,
        event: TimedEvent,
        event_id: EventId,
    ) -> States {
        match event {
            TimedEvent::Authenticating => match self {
                States::Authenticating(mut state) => {
                    state.on_timeout(sta, event_id);
                    state.transition_to(Joined).into()
                }
                _ => {
                    error!("received Authenticating timeout in unexpected state; ignoring timeout");
                    self
                }
            },
            TimedEvent::Associating => match self {
                States::Associating(state) => {
                    state.on_timeout(sta);
                    state.transition_to(Authenticated).into()
                }
                _ => {
                    error!("received Associating timeout in unexpected state; ignoring timeout");
                    self
                }
            },
            TimedEvent::AssociationStatusCheck => {
                match self {
                    States::Associated(mut state) => {
                        let should_auto_deauth = state.on_timeout(sta);
                        match should_auto_deauth {
                            true => state.transition_to(Joined).into(),
                            false => state.into(),
                        }
                    }
                    _ => {
                        error!("received association status update timeout in unexpected state; ignoring");
                        self
                    }
                }
            }
            event => {
                error!("unsupported event, {:?}, this should NOT happen", event);
                self
            }
        }
    }

    #[allow(deprecated)] // Allow until main message loop is in Rust.
    pub fn handle_mlme_msg(
        self,
        sta: &mut BoundClient<'_>,
        msg: fidl_mlme::MlmeRequestMessage,
    ) -> States {
        use fidl_mlme::MlmeRequestMessage as MlmeMsg;

        match self {
            States::Joined(state) => match msg {
                MlmeMsg::AuthenticateReq { req } => {
                    match state.on_sme_authenticate(
                        sta,
                        req.auth_failure_timeout as u16,
                        req.auth_type,
                    ) {
                        Ok(algorithm) => state.transition_to(Authenticating { algorithm }).into(),
                        Err(()) => state.into(),
                    }
                }
                _ => state.into(),
            },
            States::Authenticating(mut state) => match msg {
                MlmeMsg::SaeHandshakeResp { resp } => match state.on_sme_sae_resp(sta, resp) {
                    akm::AkmState::InProgress => state.into(),
                    akm::AkmState::Failed => state.transition_to(Joined).into(),
                    akm::AkmState::AuthComplete => state.transition_to(Authenticated).into(),
                },
                MlmeMsg::SaeFrameTx { frame } => match state.on_sme_sae_tx(sta, frame) {
                    akm::AkmState::InProgress => state.into(),
                    akm::AkmState::Failed => state.transition_to(Joined).into(),
                    akm::AkmState::AuthComplete => state.transition_to(Authenticated).into(),
                },
                MlmeMsg::DeauthenticateReq { req: _ } => {
                    state.on_sme_deauthenticate(sta);
                    state.transition_to(Joined).into()
                }
                _ => state.into(),
            },
            States::Authenticated(state) => match msg {
                MlmeMsg::AssociateReq { req } => match state.on_sme_associate(sta, req) {
                    Ok(timeout) => state.transition_to(Associating { timeout }).into(),
                    Err(()) => state.into(),
                },
                MlmeMsg::DeauthenticateReq { req } => {
                    state.on_sme_deauthenticate(sta, req);
                    state.transition_to(Joined).into()
                }
                _ => state.into(),
            },
            States::Associating(state) => match msg {
                MlmeMsg::DeauthenticateReq { req: _ } => {
                    state.on_sme_deauthenticate(sta);
                    state.transition_to(Joined).into()
                }
                _ => state.into(),
            },
            States::Associated(mut state) => match msg {
                MlmeMsg::FinalizeAssociationReq { cap } => {
                    state.on_sme_finalize_association(sta, cap);
                    state.into()
                }
                MlmeMsg::EapolReq { req } => {
                    state.on_sme_eapol(sta, req);
                    state.into()
                }
                MlmeMsg::SetKeysReq { req } => {
                    state.on_sme_set_keys(sta, req);
                    state.into()
                }
                MlmeMsg::SetControlledPort { req } => {
                    state.on_sme_set_controlled_port(sta, req);
                    state.into()
                }
                MlmeMsg::DeauthenticateReq { req } => {
                    state.on_sme_deauthenticate(sta, req);
                    state.transition_to(Joined).into()
                }
                _ => state.into(),
            },
        }
    }

    /// Returns |true| iff a given FrameClass is permitted to be processed in the current state.
    fn is_frame_class_permitted(&self, class: mac::FrameClass) -> bool {
        match self {
            States::Joined(_) | States::Authenticating(_) => class == mac::FrameClass::Class1,
            States::Authenticated(_) | States::Associating(_) => class <= mac::FrameClass::Class2,
            States::Associated(_) => class <= mac::FrameClass::Class3,
        }
    }

    pub fn pre_switch_off_channel(&mut self, sta: &mut Client, ctx: &mut Context) {
        match self {
            States::Associated(state) => {
                state.off_channel(sta, ctx);
            }
            _ => (),
        }
    }

    pub fn handle_back_on_channel(&mut self, sta: &mut Client, ctx: &mut Context) {
        match self {
            States::Associated(state) => {
                state.on_channel(sta, ctx);
            }
            _ => (),
        }
    }
}

#[cfg(test)]
mod free_function_tests {
    use super::*;

    #[test]
    fn test_extract_ht_vht_op_success() {
        let mut buf = Vec::<u8>::new();
        ie::write_ht_operation(&mut buf, &ie::fake_ht_operation()).expect("valid HT Op");
        ie::write_vht_operation(&mut buf, &ie::fake_vht_operation()).expect("valid VHT Op");
        let (ht_bytes, vht_bytes) = extract_ht_vht_op(&buf[..]);
        assert_eq!(ht_bytes.unwrap(), ie::fake_ht_op_bytes());
        assert_eq!(vht_bytes.unwrap(), ie::fake_vht_op_bytes());
    }

    #[test]
    fn test_extract_ht_op_too_short() {
        let mut buf = Vec::<u8>::new();
        ie::write_ht_operation(&mut buf, &ie::fake_ht_operation()).expect("valid HT Op");
        buf[1] -= 1; // Make length shorter
        buf.truncate(buf.len() - 1);
        ie::write_vht_operation(&mut buf, &ie::fake_vht_operation()).expect("valid VHT Op");
        let (ht_bytes, vht_bytes) = extract_ht_vht_op(&buf[..]);
        assert_eq!(ht_bytes, None);
        assert_eq!(vht_bytes.unwrap(), ie::fake_vht_op_bytes());
    }

    #[test]
    fn test_extract_vht_op_too_short() {
        let mut buf = Vec::<u8>::new();
        ie::write_ht_operation(&mut buf, &ie::fake_ht_operation()).expect("valid HT Op");
        let ht_end = buf.len();
        ie::write_vht_operation(&mut buf, &ie::fake_vht_operation()).expect("valid VHT Op");
        buf[ht_end + 1] -= 1; // Make VHT operation shorter.
        buf.truncate(buf.len() - 1);
        let (ht_bytes, vht_bytes) = extract_ht_vht_op(&buf[..]);
        assert_eq!(ht_bytes.unwrap(), ie::fake_ht_op_bytes());
        assert_eq!(vht_bytes, None);
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            block_ack::{write_addba_req_body, ADDBA_REQ_FRAME_LEN},
            buffer::FakeBufferProvider,
            client::{
                channel_listener::ChannelListenerState, channel_scheduler::ChannelScheduler,
                scanner::Scanner, Client, ClientConfig, Context,
            },
            device::{Device, FakeDevice},
        },
        akm::AkmAlgorithm,
        banjo_ddk_protocol_wlan_info as banjo_wlan_info, fidl_fuchsia_wlan_common as fidl_common,
        fuchsia_zircon::{self as zx, DurationNum},
        wlan_common::{
            assert_variant,
            buffer_writer::BufferWriter,
            mac::{Bssid, MacAddr},
            mgmt_writer,
            sequence::SequenceManager,
            test_utils::fake_frames::*,
        },
        wlan_frame_writer::write_frame_with_dynamic_buf,
        wlan_statemachine as statemachine,
    };

    const BSSID: Bssid = Bssid([6u8; 6]);
    const IFACE_MAC: MacAddr = [3u8; 6];

    struct MockObjects {
        fake_device: FakeDevice,
        fake_scheduler: FakeScheduler,
        scanner: Scanner,
        chan_sched: ChannelScheduler,
        channel_state: ChannelListenerState,
    }

    impl MockObjects {
        fn new() -> Self {
            Self {
                fake_device: FakeDevice::new(),
                fake_scheduler: FakeScheduler::new(),
                scanner: Scanner::new(IFACE_MAC),
                chan_sched: ChannelScheduler::new(),
                channel_state: Default::default(),
            }
        }

        fn make_ctx(&mut self) -> Context {
            let device = self.fake_device.as_device();
            device.set_channel(fake_wlan_channel()).expect("fake device is obedient");
            self.channel_state.main_channel = Some(fake_wlan_channel());
            self.make_ctx_with_device(device)
        }

        fn make_ctx_with_device(&mut self, device: Device) -> Context {
            let timer = Timer::<TimedEvent>::new(self.fake_scheduler.as_scheduler());
            Context {
                config: ClientConfig { ensure_on_channel_time: 0 },
                device,
                buf_provider: FakeBufferProvider::new(),
                timer,
                seq_mgr: SequenceManager::new(),
            }
        }
    }

    fn fake_wlan_channel() -> banjo_wlan_info::WlanChannel {
        banjo_wlan_info::WlanChannel {
            primary: 0,
            cbw: banjo_wlan_info::WlanChannelBandwidth(0),
            secondary80: 0,
        }
    }

    fn make_client_station() -> Client {
        Client::new(vec![], BSSID, IFACE_MAC, TimeUnit::DEFAULT_BEACON_INTERVAL.into(), false)
    }

    fn make_protected_client_station() -> Client {
        Client::new(vec![], BSSID, IFACE_MAC, TimeUnit::DEFAULT_BEACON_INTERVAL.into(), true)
    }

    fn empty_associate_conf() -> fidl_mlme::AssociateConfirm {
        fidl_mlme::AssociateConfirm {
            association_id: 0,
            result_code: fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified,
            cap_info: 0,
            rates: vec![],
            wmm_param: None,
            ht_cap: None,
            vht_cap: None,
        }
    }

    fn empty_association(sta: &mut BoundClient<'_>) -> Association {
        let status_check_timeout =
            schedule_association_status_timeout(sta.sta.beacon_period, &mut sta.ctx.timer);
        Association {
            controlled_port_open: false,
            aid: 0,
            ap_ht_op: None,
            ap_vht_op: None,
            lost_bss_counter: LostBssCounter::start(
                sta.sta.beacon_period,
                DEFAULT_AUTO_DEAUTH_TIMEOUT_BEACON_COUNT,
            ),
            qos: Qos::Disabled,
            status_check_timeout,
            signal_strength_average: SignalStrengthAverage::new(),
            block_ack_state: StateMachine::new(BlockAckState::from(State::new(Closed))),
        }
    }

    fn fake_ddk_assoc_ctx() -> banjo_wlan_info::WlanAssocCtx {
        ddk::build_ddk_assoc_ctx(
            BSSID,
            42,
            fidl_mlme::NegotiatedCapabilities {
                channel: fidl_common::WlanChan {
                    primary: 149,
                    cbw: fidl_common::Cbw::Cbw40,
                    secondary80: 42,
                },
                cap_info: 0,
                rates: vec![],
                wmm_param: None,
                ht_cap: None,
                vht_cap: None,
            },
            None,
            None,
        )
    }

    #[allow(deprecated)] // Raw MLME messages are deprecated.
    fn fake_mlme_deauth_req() -> fidl_mlme::MlmeRequestMessage {
        fidl_mlme::MlmeRequestMessage::DeauthenticateReq {
            req: fidl_mlme::DeauthenticateRequest {
                peer_sta_address: BSSID.0,
                reason_code: fidl_mlme::ReasonCode::LeavingNetworkDeauth,
            },
        }
    }

    fn empty_authenticate_request() -> fidl_mlme::AuthenticateRequest {
        fidl_mlme::AuthenticateRequest {
            peer_sta_address: [0; 6],
            auth_type: fidl_mlme::AuthenticationTypes::OpenSystem,
            auth_failure_timeout: 0,
        }
    }

    fn empty_associate_request() -> fidl_mlme::AssociateRequest {
        fidl_mlme::AssociateRequest {
            peer_sta_address: [1, 2, 3, 4, 5, 6],
            cap_info: 0,
            rates: vec![1, 2, 3],
            qos_capable: false,
            qos_info: 0,
            ht_cap: None,
            vht_cap: None,
            rsne: None,
            vendor_ies: None,
        }
    }

    fn open_authenticating(sta: &mut BoundClient<'_>) -> Authenticating {
        let mut auth = Authenticating {
            algorithm: AkmAlgorithm::open_supplicant(TimeUnit(2 * sta.sta.beacon_period)),
        };
        auth.algorithm.initiate(sta).expect("Failed to initiate open auth");
        auth
    }

    #[test]
    fn join_state_authenticate_success() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let state = Joined;
        state
            .on_sme_authenticate(&mut sta, 10, fidl_mlme::AuthenticationTypes::OpenSystem)
            .expect("failed authenticating");

        // Verify an event was queued up in the timer.
        assert_eq!(sta.ctx.timer.scheduled_events(TimedEvent::Authenticating).len(), 1);

        // Verify authentication frame was sent to AP.
        assert_eq!(m.fake_device.wlan_queue.len(), 1);
        let (frame, _txflags) = m.fake_device.wlan_queue.remove(0);
        #[rustfmt::skip]
        let expected = vec![
            // Mgmt Header:
            0b1011_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // Addr1
            3, 3, 3, 3, 3, 3, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Auth Header:
            0, 0, // Algorithm Number (Open)
            1, 0, // Txn Sequence Number
            0, 0, // Status Code
        ];
        assert_eq!(&frame[..], &expected[..]);

        // Verify no MLME message was sent yet.
        m.fake_device
            .next_mlme_msg::<fidl_mlme::AuthenticateConfirm>()
            .expect_err("unexpected message");
    }

    #[test]
    fn join_state_authenticate_tx_failure() {
        let mut m = MockObjects::new();
        let device = m.fake_device.as_device_fail_wlan_tx();
        let mut ctx = m.make_ctx_with_device(device);
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);

        let state = Joined;
        state
            .on_sme_authenticate(&mut sta, 10, fidl_mlme::AuthenticationTypes::OpenSystem)
            .expect_err("should fail authenticating");

        // Verify no event was queued up in the timer.
        assert_eq!(sta.ctx.timer.scheduled_event_count(), 0);

        // Verify MLME-AUTHENTICATE.confirm message was sent.
        let msg =
            m.fake_device.next_mlme_msg::<fidl_mlme::AuthenticateConfirm>().expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::AuthenticateConfirm {
                peer_sta_address: BSSID.0,
                auth_type: fidl_mlme::AuthenticationTypes::OpenSystem,
                result_code: fidl_mlme::AuthenticateResultCodes::Refused,
            }
        );
    }

    #[test]
    fn authenticating_state_auth_success() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let mut state = open_authenticating(&mut sta);

        // Verify authentication was considered successful.
        assert_variant!(
            state.on_auth_frame(
                &mut sta,
                &mac::AuthHdr {
                    auth_alg_num: mac::AuthAlgorithmNumber::OPEN,
                    auth_txn_seq_num: 2,
                    status_code: mac::StatusCode::SUCCESS,
                },
                &[]
            ),
            akm::AkmState::AuthComplete
        );

        // Verify timeout was canceled.
        assert_eq!(sta.ctx.timer.scheduled_events(TimedEvent::Authenticating).len(), 0);

        // Verify MLME-AUTHENTICATE.confirm message was sent.
        let msg =
            m.fake_device.next_mlme_msg::<fidl_mlme::AuthenticateConfirm>().expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::AuthenticateConfirm {
                peer_sta_address: BSSID.0,
                auth_type: fidl_mlme::AuthenticationTypes::OpenSystem,
                result_code: fidl_mlme::AuthenticateResultCodes::Success,
            }
        );
    }

    #[test]
    fn authenticating_state_auth_rejected() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let mut state = open_authenticating(&mut sta);

        // Verify authentication failed.
        assert_variant!(
            state.on_auth_frame(
                &mut sta,
                &mac::AuthHdr {
                    auth_alg_num: mac::AuthAlgorithmNumber::OPEN,
                    auth_txn_seq_num: 2,
                    status_code: mac::StatusCode::NOT_IN_SAME_BSS,
                },
                &[]
            ),
            akm::AkmState::Failed
        );

        // Verify timeout was canceled.
        assert_eq!(sta.ctx.timer.scheduled_events(TimedEvent::Authenticating).len(), 0);

        // Verify MLME-AUTHENTICATE.confirm message was sent.
        let msg =
            m.fake_device.next_mlme_msg::<fidl_mlme::AuthenticateConfirm>().expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::AuthenticateConfirm {
                peer_sta_address: BSSID.0,
                auth_type: fidl_mlme::AuthenticationTypes::OpenSystem,
                result_code: fidl_mlme::AuthenticateResultCodes::AuthenticationRejected,
            }
        );
    }

    #[test]
    fn authenticating_state_timeout() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let mut state = open_authenticating(&mut sta);

        let timeout = sta.ctx.timer.scheduled_events(TimedEvent::Authenticating)[0];
        state.on_timeout(&mut sta, timeout);

        // Verify timeout was canceled.
        assert_variant!(sta.ctx.timer.triggered(&timeout), None);

        // Verify MLME-AUTHENTICATE.confirm message was sent.
        let msg =
            m.fake_device.next_mlme_msg::<fidl_mlme::AuthenticateConfirm>().expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::AuthenticateConfirm {
                peer_sta_address: BSSID.0,
                auth_type: fidl_mlme::AuthenticationTypes::OpenSystem,
                result_code: fidl_mlme::AuthenticateResultCodes::AuthFailureTimeout,
            }
        );
    }

    #[test]
    fn authenticating_state_deauth_frame() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let mut state = open_authenticating(&mut sta);

        state.on_deauth_frame(
            &mut sta,
            &mac::DeauthHdr { reason_code: mac::ReasonCode::NO_MORE_STAS },
        );

        // Verify timeout was canceled.
        assert_eq!(sta.ctx.timer.scheduled_events(TimedEvent::Authenticating).len(), 0);

        // Verify MLME-AUTHENTICATE.confirm message was sent.
        let msg =
            m.fake_device.next_mlme_msg::<fidl_mlme::AuthenticateConfirm>().expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::AuthenticateConfirm {
                peer_sta_address: BSSID.0,
                auth_type: fidl_mlme::AuthenticationTypes::OpenSystem,
                result_code: fidl_mlme::AuthenticateResultCodes::Refused,
            }
        );
    }

    #[test]
    fn authenticated_state_deauth_frame() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let state = Authenticated;

        state.on_deauth_frame(
            &mut sta,
            &mac::DeauthHdr { reason_code: mac::ReasonCode::NO_MORE_STAS },
        );

        // Verify MLME-DEAUTHENTICATE.indication message was sent.
        let msg = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::DeauthenticateIndication>()
            .expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::DeauthenticateIndication {
                peer_sta_address: BSSID.0,
                reason_code: fidl_mlme::ReasonCode::NoMoreStas,
                locally_initiated: false,
            }
        );
    }

    #[test]
    fn associating_success_unprotected() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);

        let timeout = sta
            .ctx
            .timer
            .schedule_event(sta.ctx.timer.now() + 1.seconds(), TimedEvent::Associating);
        let state = Associating { timeout };

        let Association { aid, controlled_port_open, .. } = state
            .on_assoc_resp_frame(
                &mut sta,
                &mac::AssocRespHdr {
                    aid: 42,
                    capabilities: mac::CapabilityInfo(52),
                    status_code: mac::StatusCode::SUCCESS,
                },
                &[][..],
            )
            .expect("failed processing association response frame");
        assert_eq!(aid, 42);
        assert_eq!(true, controlled_port_open);

        // Verify timeout was canceled.
        assert_variant!(sta.ctx.timer.triggered(&timeout), None);

        // Verify MLME-ASSOCIATE.confirm message was sent.
        let msg = m.fake_device.next_mlme_msg::<fidl_mlme::AssociateConfirm>().expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::AssociateConfirm {
                association_id: 42,
                result_code: fidl_mlme::AssociateResultCodes::Success,
                cap_info: 52,
                ..empty_associate_conf()
            }
        );
    }

    #[test]
    fn associating_success_protected() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_protected_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let timeout =
            sta.ctx.timer.schedule_event(zx::Time::after(1.seconds()), TimedEvent::Associating);
        let state = Associating { timeout };

        let Association { aid, controlled_port_open, .. } = state
            .on_assoc_resp_frame(
                &mut sta,
                &mac::AssocRespHdr {
                    aid: 42,
                    capabilities: mac::CapabilityInfo(52),
                    status_code: mac::StatusCode::SUCCESS,
                },
                &[][..],
            )
            .expect("failed processing association response frame");
        assert_eq!(aid, 42);
        assert_eq!(false, controlled_port_open);

        // Verify timeout was canceled.
        assert_variant!(sta.ctx.timer.triggered(&timeout), None);

        // Verify MLME-ASSOCIATE.confirm message was sent.
        let msg = m.fake_device.next_mlme_msg::<fidl_mlme::AssociateConfirm>().expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::AssociateConfirm {
                association_id: 42,
                result_code: fidl_mlme::AssociateResultCodes::Success,
                cap_info: 52,
                ..empty_associate_conf()
            }
        );
    }

    #[test]
    fn associating_failure() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);

        let timeout =
            sta.ctx.timer.schedule_event(zx::Time::after(1.seconds()), TimedEvent::Associating);
        let state = Associating { timeout };

        // Verify authentication was considered successful.
        state
            .on_assoc_resp_frame(
                &mut sta,
                &mac::AssocRespHdr {
                    aid: 42,
                    capabilities: mac::CapabilityInfo(52),
                    status_code: mac::StatusCode::NOT_IN_SAME_BSS,
                },
                &[][..],
            )
            .expect_err("expected failure processing association response frame");

        // Verify timeout was canceled.
        assert_variant!(sta.ctx.timer.triggered(&timeout), None);

        // Verify MLME-ASSOCIATE.confirm message was sent.
        let msg = m.fake_device.next_mlme_msg::<fidl_mlme::AssociateConfirm>().expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::AssociateConfirm {
                association_id: 0,
                result_code: fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified,
                ..empty_associate_conf()
            }
        );
    }

    #[test]
    fn associating_timeout() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);

        let timeout =
            sta.ctx.timer.schedule_event(zx::Time::after(1.seconds()), TimedEvent::Associating);
        let state = Associating { timeout };

        // Trigger timeout.
        state.on_timeout(&mut sta);

        // Verify timeout was canceled.
        assert_variant!(sta.ctx.timer.triggered(&timeout), None);

        // Verify MLME-ASSOCIATE.confirm message was sent.
        let msg = m.fake_device.next_mlme_msg::<fidl_mlme::AssociateConfirm>().expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::AssociateConfirm {
                association_id: 0,
                result_code: fidl_mlme::AssociateResultCodes::RefusedTemporarily,
                ..empty_associate_conf()
            }
        );
    }

    #[test]
    fn associating_deauth_frame() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);

        let timeout =
            sta.ctx.timer.schedule_event(zx::Time::after(1.seconds()), TimedEvent::Associating);
        let state = Associating { timeout };

        state.on_deauth_frame(
            &mut sta,
            &mac::DeauthHdr { reason_code: mac::ReasonCode::AP_INITIATED },
        );

        // Verify timeout was canceled.
        assert_variant!(sta.ctx.timer.triggered(&timeout), None);

        // Verify MLME-ASSOCIATE.confirm message was sent.
        let msg = m.fake_device.next_mlme_msg::<fidl_mlme::AssociateConfirm>().expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::AssociateConfirm {
                association_id: 0,
                result_code: fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified,
                ..empty_associate_conf()
            }
        );
    }

    #[test]
    fn associating_disassociation() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);

        let timeout =
            sta.ctx.timer.schedule_event(zx::Time::after(1.seconds()), TimedEvent::Associating);
        let state = Associating { timeout };

        state.on_disassoc_frame(
            &mut sta,
            &mac::DisassocHdr { reason_code: mac::ReasonCode::AP_INITIATED },
        );

        // Verify timeout was canceled.
        assert_variant!(sta.ctx.timer.triggered(&timeout), None);

        // Verify MLME-ASSOCIATE.confirm message was sent.
        let msg = m.fake_device.next_mlme_msg::<fidl_mlme::AssociateConfirm>().expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::AssociateConfirm {
                association_id: 0,
                result_code: fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified,
                ..empty_associate_conf()
            }
        );
    }

    #[test]
    fn associated_block_ack_frame() {
        let mut mock = MockObjects::new();
        let mut ctx = mock.make_ctx();
        let mut station = make_client_station();
        let mut client = station.bind(
            &mut ctx,
            &mut mock.scanner,
            &mut mock.chan_sched,
            &mut mock.channel_state,
        );

        let frame = {
            let mut buffer = [0u8; ADDBA_REQ_FRAME_LEN];
            let writer = BufferWriter::new(&mut buffer[..]);
            let (mut writer, _) = write_frame_with_dynamic_buf!(
                writer,
                {
                    headers: {
                        mac::MgmtHdr: &mgmt_writer::mgmt_hdr_from_ap(
                            mac::FrameControl(0)
                                .with_frame_type(mac::FrameType::MGMT)
                                .with_mgmt_subtype(mac::MgmtSubtype::ACTION),
                            client.sta.iface_mac,
                            client.sta.bssid,
                            mac::SequenceControl(0)
                                .with_seq_num(client.ctx.seq_mgr.next_sns1(&client.sta.bssid.0) as u16),
                        ),
                    },
                }
            )
            .unwrap();
            write_addba_req_body(&mut writer, 1).unwrap();
            buffer
        };

        let state = States::from(statemachine::testing::new_state(Associated(empty_association(
            &mut client,
        ))));
        match state.on_mac_frame(&mut client, &frame[..], None) {
            States::Associated(state) => {
                let (_, associated) = state.release_data();
                match *associated.0.block_ack_state.as_ref() {
                    BlockAckState::Established(_) => {}
                    _ => panic!("client has not established BlockAck"),
                }
            }
            _ => panic!("client no longer associated"),
        }
    }

    #[test]
    fn associated_deauth_frame() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let mut state = Associated(empty_association(&mut sta));

        // ddk_assoc_ctx will be cleared when MLME receives deauth frame.
        sta.ctx
            .device
            .configure_assoc(fake_ddk_assoc_ctx())
            .expect("valid assoc_ctx should succeed");
        assert_eq!(1, m.fake_device.assocs.len());

        sta.ctx.device.set_eth_link_up().expect("should succeed");
        assert_eq!(m.fake_device.link_status, crate::device::LinkStatus::UP);

        state.on_deauth_frame(
            &mut sta,
            &mac::DeauthHdr { reason_code: mac::ReasonCode::AP_INITIATED },
        );

        // Verify MLME-ASSOCIATE.confirm message was sent.
        let msg = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::DeauthenticateIndication>()
            .expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::DeauthenticateIndication {
                peer_sta_address: BSSID.0,
                reason_code: fidl_mlme::ReasonCode::ApInitiated,
                locally_initiated: false,
            }
        );
        // Verify association context is cleared and ethernet port is shut down.
        assert_eq!(0, m.fake_device.assocs.len());
        assert_eq!(m.fake_device.link_status, crate::device::LinkStatus::DOWN);
    }

    #[test]
    fn associated_disassociation() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let mut state = Associated(empty_association(&mut sta));

        state.0.controlled_port_open = true;
        // ddk_assoc_ctx must be cleared when MLME receives disassociation frame later.
        sta.ctx
            .device
            .configure_assoc(fake_ddk_assoc_ctx())
            .expect("valid assoc_ctx should succeed");
        assert_eq!(1, m.fake_device.assocs.len());

        sta.ctx.device.set_eth_link_up().expect("should succeed");
        assert_eq!(m.fake_device.link_status, crate::device::LinkStatus::UP);

        state.on_disassoc_frame(
            &mut sta,
            &mac::DisassocHdr { reason_code: mac::ReasonCode::AP_INITIATED },
        );

        // Verify MLME-ASSOCIATE.confirm message was sent.
        let msg =
            m.fake_device.next_mlme_msg::<fidl_mlme::DisassociateIndication>().expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::DisassociateIndication {
                peer_sta_address: BSSID.0,
                reason_code: mac::ReasonCode::AP_INITIATED.0,
                locally_initiated: false,
            }
        );

        // Verify association everything is properly cleared.
        assert_eq!(0, sta.ctx.timer.scheduled_event_count());
        assert_eq!(false, state.0.controlled_port_open);
        assert_eq!(0, m.fake_device.assocs.len());
        assert_eq!(m.fake_device.link_status, crate::device::LinkStatus::DOWN);
    }

    #[test]
    fn associated_move_data_closed_controlled_port() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let state = Associated(empty_association(&mut sta));

        let data_frame = make_data_frame_single_llc(None, None);
        let (fixed, addr4, qos, body) = parse_data_frame(&data_frame[..]);
        state.on_data_frame(&mut sta, &fixed, addr4, qos, body);

        // Verify data frame was dropped.
        assert_eq!(m.fake_device.eth_queue.len(), 0);
    }

    #[test]
    fn associated_move_data_opened_controlled_port() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let state =
            Associated(Association { controlled_port_open: true, ..empty_association(&mut sta) });

        let data_frame = make_data_frame_single_llc(None, None);
        let (fixed, addr4, qos, body) = parse_data_frame(&data_frame[..]);
        state.on_data_frame(&mut sta, &fixed, addr4, qos, body);

        // Verify data frame was processed.
        assert_eq!(m.fake_device.eth_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(m.fake_device.eth_queue[0], [
            3, 3, 3, 3, 3, 3, // dst_addr
            4, 4, 4, 4, 4, 4, // src_addr
            9, 10, // ether_type
            11, 11, 11, // payload
        ]);
    }

    #[test]
    fn associated_handle_eapol_closed_controlled_port() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_protected_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let state = Associated(empty_association(&mut sta));

        let (src_addr, dst_addr, eapol_frame) = make_eapol_frame(IFACE_MAC);
        let (fixed, addr4, qos, body) = parse_data_frame(&eapol_frame[..]);
        state.on_data_frame(&mut sta, &fixed, addr4, qos, body);

        // Verify EAPOL frame was not sent to netstack.
        assert_eq!(m.fake_device.eth_queue.len(), 0);

        // Verify EAPoL frame was sent to SME.
        let eapol_ind = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::EapolIndication>()
            .expect("error reading EAPOL.indication");
        assert_eq!(
            eapol_ind,
            fidl_mlme::EapolIndication { src_addr, dst_addr, data: EAPOL_PDU.to_vec() }
        );
    }

    #[test]
    fn associated_handle_eapol_open_controlled_port() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_protected_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let state = Associated(empty_association(&mut sta));

        let (src_addr, dst_addr, eapol_frame) = make_eapol_frame(IFACE_MAC);
        let (fixed, addr4, qos, body) = parse_data_frame(&eapol_frame[..]);
        state.on_data_frame(&mut sta, &fixed, addr4, qos, body);

        // Verify EAPOL frame was not sent to netstack.
        assert_eq!(m.fake_device.eth_queue.len(), 0);

        // Verify EAPoL frame was sent to SME.
        let eapol_ind = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::EapolIndication>()
            .expect("error reading EAPOL.indication");
        assert_eq!(
            eapol_ind,
            fidl_mlme::EapolIndication { src_addr, dst_addr, data: EAPOL_PDU.to_vec() }
        );
    }

    #[test]
    fn associated_handle_amsdus_open_controlled_port() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_protected_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let state =
            Associated(Association { controlled_port_open: true, ..empty_association(&mut sta) });

        let data_frame = make_data_frame_amsdu();
        let (fixed, addr4, qos, body) = parse_data_frame(&data_frame[..]);
        state.on_data_frame(&mut sta, &fixed, addr4, qos, body);

        let queue = &m.fake_device.eth_queue;
        assert_eq!(queue.len(), 2);
        #[rustfmt::skip]
        let mut expected_first_eth_frame = vec![
            0x78, 0x8a, 0x20, 0x0d, 0x67, 0x03, // dst_addr
            0xb4, 0xf7, 0xa1, 0xbe, 0xb9, 0xab, // src_addr
            0x08, 0x00, // ether_type
        ];
        expected_first_eth_frame.extend_from_slice(MSDU_1_PAYLOAD);
        assert_eq!(queue[0], &expected_first_eth_frame[..]);
        #[rustfmt::skip]
        let mut expected_second_eth_frame = vec![
            0x78, 0x8a, 0x20, 0x0d, 0x67, 0x04, // dst_addr
            0xb4, 0xf7, 0xa1, 0xbe, 0xb9, 0xac, // src_addr
            0x08, 0x01, // ether_type
        ];
        expected_second_eth_frame.extend_from_slice(MSDU_2_PAYLOAD);
        assert_eq!(queue[1], &expected_second_eth_frame[..]);
    }

    #[test]
    fn associated_request_bu_data_frame() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let state = Associated(Association {
            aid: 42,
            controlled_port_open: true,
            ..empty_association(&mut sta)
        });

        let data_frame = make_data_frame_single_llc(None, None);
        let (mut fixed, addr4, qos, body) = parse_data_frame(&data_frame[..]);
        fixed.frame_ctrl = fixed.frame_ctrl.with_more_data(true);
        state.on_data_frame(&mut sta, &fixed, addr4, qos, body);

        assert_eq!(m.fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&m
            .fake_device.wlan_queue[0].0[..], &[
            // Frame Control:
            0b10100100, 0b00000000, // FC
            42, 0b11_000000, // Id
            6, 6, 6, 6, 6, 6, // addr1
            3, 3, 3, 3, 3, 3, // addr2
        ][..]);
    }

    #[test]
    fn associated_request_bu_mgmt_frame() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let state = Associated(Association {
            aid: 42,
            controlled_port_open: true,
            ..empty_association(&mut sta)
        });

        state.on_any_mgmt_frame(
            &mut sta,
            &mac::MgmtHdr {
                frame_ctrl: mac::FrameControl(0)
                    .with_frame_type(mac::FrameType::MGMT)
                    .with_mgmt_subtype(mac::MgmtSubtype::BEACON)
                    .with_more_data(true),
                duration: 0,
                addr1: [3; 6],
                addr2: BSSID.0,
                addr3: BSSID.0,
                seq_ctrl: mac::SequenceControl(0),
            },
        );

        assert_eq!(m.fake_device.wlan_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(&m
            .fake_device.wlan_queue[0].0[..], &[
            // Frame Control:
            0b10100100, 0b00000000, // FC
            42, 0b11_000000, // Id
            6, 6, 6, 6, 6, 6, // addr1
            3, 3, 3, 3, 3, 3, // addr2
        ][..]);
    }

    #[test]
    fn associated_no_bu_request() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);

        // Closed Controlled port
        let state = Associated(empty_association(&mut sta));
        let data_frame = make_data_frame_single_llc(None, None);
        let (mut fixed, addr4, qos, body) = parse_data_frame(&data_frame[..]);
        fixed.frame_ctrl = fixed.frame_ctrl.with_more_data(true);
        state.on_data_frame(&mut sta, &fixed, addr4, qos, body);
        assert_eq!(m.fake_device.wlan_queue.len(), 0);

        // Foreign management frame
        let state = States::from(statemachine::testing::new_state(Associated(Association {
            controlled_port_open: true,
            ..empty_association(&mut sta)
        })));
        #[rustfmt::skip]
        let beacon = vec![
            // Mgmt Header:
            0b1000_00_00, 0b00100000, // Frame Control
            0, 0, // Duration
            3, 3, 3, 3, 3, 3, // Addr1
            7, 7, 7, 7, 7, 7, // Addr2
            5, 5, 5, 5, 5, 5, // Addr3
            0x10, 0, // Sequence Control
            // Omit IEs
        ];
        state.on_mac_frame(&mut sta, &beacon[..], None);
        assert_eq!(m.fake_device.wlan_queue.len(), 0);
    }

    #[test]
    fn associated_drop_foreign_data_frames() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);

        // Foreign data frame
        let state = States::from(statemachine::testing::new_state(Associated(Association {
            aid: 42,
            controlled_port_open: true,
            ..empty_association(&mut sta)
        })));
        let fc = mac::FrameControl(0)
            .with_frame_type(mac::FrameType::DATA)
            .with_data_subtype(mac::DataSubtype(0))
            .with_from_ds(true);
        let fc = fc.0.to_le_bytes();
        // Send data frame from an address other than the BSSID([6u8; 6]).
        let bytes = vec![
            // Data Header
            fc[0], fc[1], // fc
            2, 2, // duration
            3, 3, 3, 3, 3, 3, // addr1
            4, 4, 4, 4, 4, 4, // addr2
            5, 5, 5, 5, 5, 5, // addr3
            6, 6, // sequence control
            // LLC Header
            7, 7, 7, // DSAP, SSAP & control
            8, 8, 8, // OUI
            9, 10, // eth type
            // Trailing bytes
            11, 11, 11,
        ];
        state.on_mac_frame(&mut sta, &bytes[..], None);
        assert_eq!(m.fake_device.eth_queue.len(), 0);
    }

    #[test]
    #[allow(deprecated)] // for raw MlmeRequestMessage
    fn state_transitions_joined_authing() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let mut state = States::new_initial();
        assert_variant!(state, States::Joined(_), "not in joined state");

        // Successful: Joined > Authenticating
        state = state.handle_mlme_msg(
            &mut sta,
            fidl_mlme::MlmeRequestMessage::AuthenticateReq {
                req: fidl_mlme::AuthenticateRequest {
                    auth_failure_timeout: 10,
                    ..empty_authenticate_request()
                },
            },
        );
        assert_variant!(state, States::Authenticating(_), "not in auth'ing state");
    }

    #[test]
    fn state_transitions_authing_success() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let mut state =
            States::from(statemachine::testing::new_state(open_authenticating(&mut sta)));

        // Successful: Joined > Authenticating > Authenticated
        #[rustfmt::skip]
        let auth_resp_success = vec![
            // Mgmt Header:
            0b1011_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            3, 3, 3, 3, 3, 3, // Addr1 == IFACE_MAC
            3, 3, 3, 3, 3, 3, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Auth Header:
            0, 0, // Algorithm Number (Open)
            2, 0, // Txn Sequence Number
            0, 0, // Status Code
        ];
        state = state.on_mac_frame(&mut sta, &auth_resp_success[..], None);
        assert_variant!(state, States::Authenticated(_), "not in auth'ed state");
    }

    #[test]
    fn state_transitions_authing_failure() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let mut state =
            States::from(statemachine::testing::new_state(open_authenticating(&mut sta)));

        // Failure: Joined > Authenticating > Joined
        #[rustfmt::skip]
        let auth_resp_failure = vec![
            // Mgmt Header:
            0b1011_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            3, 3, 3, 3, 3, 3, // Addr1 == IFACE_MAC
            3, 3, 3, 3, 3, 3, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Auth Header:
            0, 0, // Algorithm Number (Open)
            2, 0, // Txn Sequence Number
            42, 0, // Status Code
        ];
        state = state.on_mac_frame(&mut sta, &auth_resp_failure[..], None);
        assert_variant!(state, States::Joined(_), "not in joined state");
    }

    #[test]
    #[allow(deprecated)] // for raw MlmeRequestMessage
    fn state_transitions_authing_timeout() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let mut state = States::new_initial();
        assert_variant!(state, States::Joined(_), "not in joined state");

        // Timeout: Joined > Authenticating > Joined
        state = state.handle_mlme_msg(
            &mut sta,
            fidl_mlme::MlmeRequestMessage::AuthenticateReq {
                req: fidl_mlme::AuthenticateRequest {
                    auth_failure_timeout: 10,
                    ..empty_authenticate_request()
                },
            },
        );
        assert_variant!(state, States::Authenticating(_), "not in auth'ing state");
        let timeout_id = sta.ctx.timer.scheduled_events(TimedEvent::Authenticating)[0];
        let event = sta.ctx.timer.triggered(&timeout_id);
        assert_variant!(event, Some(TimedEvent::Authenticating));
        state = state.on_timed_event(&mut sta, event.unwrap(), timeout_id);
        assert_variant!(state, States::Joined(_), "not in joined state");
    }

    #[test]
    fn state_transitions_authing_deauth() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let mut state =
            States::from(statemachine::testing::new_state(open_authenticating(&mut sta)));

        // Deauthenticate: Authenticating > Joined
        #[rustfmt::skip]
        let deauth = vec![
            // Mgmt Header:
            0b1100_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            3, 3, 3, 3, 3, 3, // Addr1 == IFACE_MAC
            3, 3, 3, 3, 3, 3, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Deauth Header:
            5, 0, // Algorithm Number (Open)
        ];
        state = state.on_mac_frame(&mut sta, &deauth[..], None);
        assert_variant!(state, States::Joined(_), "not in joined state");
    }

    #[test]
    fn state_transitions_authed() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let mut state = States::from(statemachine::testing::new_state(Authenticated));

        // Deauthenticate: Authenticated > Joined
        #[rustfmt::skip]
        let deauth = vec![
            // Mgmt Header:
            0b1100_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            3, 3, 3, 3, 3, 3, // Addr1 == IFACE_MAC
            3, 3, 3, 3, 3, 3, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Deauth Header:
            5, 0, // Algorithm Number (Open)
        ];
        state = state.on_mac_frame(&mut sta, &deauth[..], None);
        assert_variant!(state, States::Joined(_), "not in joined state");
    }

    #[test]
    fn state_transitions_foreign_auth_resp() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let mut state =
            States::from(statemachine::testing::new_state(open_authenticating(&mut sta)));

        // Send foreign auth response. State should not change.
        #[rustfmt::skip]
        let auth_resp_success = vec![
            // Mgmt Header:
            0b1011_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            5, 5, 5, 5, 5, 5, // Addr1
            3, 3, 3, 3, 3, 3, // Addr2
            5, 5, 5, 5, 5, 5, // Addr3
            0x10, 0, // Sequence Control
            // Auth Header:
            0, 0, // Algorithm Number (Open)
            2, 0, // Txn Sequence Number
            0, 0, // Status Code
        ];
        state = state.on_mac_frame(&mut sta, &auth_resp_success[..], None);
        assert_variant!(state, States::Authenticating(_), "not in auth'ing state");

        // Verify that an authentication response from the joined BSS still moves the Client
        // forward.
        #[rustfmt::skip]
        let auth_resp_success = vec![
            // Mgmt Header:
            0b1011_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            3, 3, 3, 3, 3, 3, // Addr1 == IFACE_MAC
            3, 3, 3, 3, 3, 3, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Auth Header:
            0, 0, // Algorithm Number (Open)
            2, 0, // Txn Sequence Number
            0, 0, // Status Code
        ];
        state = state.on_mac_frame(&mut sta, &auth_resp_success[..], None);
        assert_variant!(state, States::Authenticated(_), "not in auth'ed state");
    }

    #[test]
    fn state_transitions_associng_success() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let mut state = States::from(statemachine::testing::new_state(Associating {
            timeout: EventId::default(),
        }));

        // Successful: Associating > Associated
        #[rustfmt::skip]
        let assoc_resp_success = vec![
            // Mgmt Header:
            0b0001_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            3, 3, 3, 3, 3, 3, // Addr1 == IFACE_MAC
            3, 3, 3, 3, 3, 3, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Assoc Resp Header:
            0, 0, // Capabilities
            0, 0, // Status Code
            0, 0, // AID
        ];
        state = state.on_mac_frame(&mut sta, &assoc_resp_success[..], None);
        assert_variant!(state, States::Associated(_), "not in associated state");
    }

    #[test]
    fn state_transitions_associng_failure() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let mut state = States::from(statemachine::testing::new_state(Associating {
            timeout: EventId::default(),
        }));

        // Failure: Associating > Authenticated
        #[rustfmt::skip]
        let assoc_resp_success = vec![
            // Mgmt Header:
            0b0001_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            3, 3, 3, 3, 3, 3, // Addr1 == IFACE_MAC
            3, 3, 3, 3, 3, 3, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Assoc Resp Header:
            0, 0, // Capabilities
            2, 0, // Status Code (Failed)
            0, 0, // AID
        ];
        state = state.on_mac_frame(&mut sta, &assoc_resp_success[..], None);
        assert_variant!(state, States::Authenticated(_), "not in authenticated state");
    }

    #[test]
    #[allow(deprecated)] // for raw MlmeRequestMessage
    fn state_transitions_associng_timeout() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let mut state = States::from(statemachine::testing::new_state(Authenticated));

        state = state.handle_mlme_msg(
            &mut sta,
            fidl_mlme::MlmeRequestMessage::AssociateReq { req: empty_associate_request() },
        );
        let timeout_id = assert_variant!(state, States::Associating(ref state) => {
                state.timeout.clone()
            }, "not in assoc'ing state");
        let event = sta.ctx.timer.triggered(&timeout_id);
        assert_variant!(event, Some(TimedEvent::Associating));
        state = state.on_timed_event(&mut sta, event.unwrap(), timeout_id);
        assert_variant!(state, States::Authenticated(_), "not in authenticated state");
    }

    #[test]
    fn state_transitions_associng_deauthing() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let mut state = States::from(statemachine::testing::new_state(Associating {
            timeout: EventId::default(),
        }));

        // Deauthentication: Associating > Joined
        #[rustfmt::skip]
        let deauth = vec![
            // Mgmt Header:
            0b1100_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            3, 3, 3, 3, 3, 3, // Addr1 == IFACE_MAC
            3, 3, 3, 3, 3, 3, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Deauth Header:
            4, 0, // Reason Code
        ];
        state = state.on_mac_frame(&mut sta, &deauth[..], None);
        assert_variant!(state, States::Joined(_), "not in joined state");
    }

    #[test]
    fn state_transitions_assoced_disassoc() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let mut state =
            States::from(statemachine::testing::new_state(Associated(empty_association(&mut sta))));

        // Disassociation: Associated > Authenticated
        #[rustfmt::skip]
        let disassoc = vec![
            // Mgmt Header:
            0b1010_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            3, 3, 3, 3, 3, 3, // Addr1 == IFACE_MAC
            3, 3, 3, 3, 3, 3, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Deauth Header:
            4, 0, // Reason Code
        ];
        state = state.on_mac_frame(&mut sta, &disassoc[..], None);
        assert_variant!(state, States::Authenticated(_), "not in authenticated state");
    }

    #[test]
    fn state_transitions_assoced_deauthing() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let mut state =
            States::from(statemachine::testing::new_state(Associated(empty_association(&mut sta))));

        // Deauthentication: Associated > Joined
        #[rustfmt::skip]
        let deauth = vec![
            // Mgmt Header:
            0b1100_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            3, 3, 3, 3, 3, 3, // Addr1 == IFACE_MAC
            3, 3, 3, 3, 3, 3, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Deauth Header:
            4, 0, // Reason Code
        ];
        state = state.on_mac_frame(&mut sta, &deauth[..], None);
        assert_variant!(state, States::Joined(_), "not in joined state");
    }

    fn parse_data_frame(
        bytes: &[u8],
    ) -> (mac::FixedDataHdrFields, Option<MacAddr>, Option<mac::QosControl>, &[u8]) {
        let parsed_frame = mac::MacFrame::parse(bytes, false).expect("invalid frame");
        match parsed_frame {
            mac::MacFrame::Data { fixed_fields, addr4, qos_ctrl, body, .. } => {
                (*fixed_fields, addr4.map(|x| *x), qos_ctrl.map(|x| x.get()), body)
            }
            _ => panic!("error parsing data frame"),
        }
    }

    #[test]
    fn assoc_send_eth_frame_becomes_data_frame() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let state = States::from(statemachine::testing::new_state(Associated(Association {
            controlled_port_open: true,
            ..empty_association(&mut sta)
        })));

        let eth_frame = [
            1, 2, 3, 4, 5, 6, // dst_addr
            11, 12, 13, 14, 15, 16, // src_addr
            0x0d, 0x05, // ether_type
            21, 22, 23, 24, 25, 26, 27, 28, // payload
            29, // more payload
        ];

        assert_eq!((), state.on_eth_frame(&mut sta, &eth_frame[..]).expect("all good"));

        assert_eq!(m.fake_device.wlan_queue.len(), 1);
        let (data_frame, _tx_flags) = m.fake_device.wlan_queue.remove(0);
        assert_eq!(
            &data_frame[..],
            &[
                // Data header
                0b00001000, 0b00000001, // Frame Control
                0, 0, // Duration
                6, 6, 6, 6, 6, 6, // addr1
                11, 12, 13, 14, 15, 16, // addr2 (from src_addr above)
                1, 2, 3, 4, 5, 6, // addr3 (from dst_addr above)
                0x10, 0, // Sequence Control
                // LLC header
                0xAA, 0xAA, 0x03, // DSAP, SSAP, Control, OUI
                0, 0, 0, // OUI
                0x0d, 0x05, // Protocol ID (from ether_type above)
                21, 22, 23, 24, 25, 26, 27, 28, // Payload
                29, // More payload
            ][..]
        )
    }

    #[test]
    fn eth_frame_dropped_when_off_channel() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let state =
            States::from(statemachine::testing::new_state(Associated(empty_association(&mut sta))));

        sta.ctx
            .device
            .set_channel(banjo_wlan_info::WlanChannel { primary: 42, ..fake_wlan_channel() })
            .expect("fake device is obedient");
        let eth_frame = &[100; 14]; // An ethernet frame must be at least 14 bytes long.

        let error = state
            .on_eth_frame(&mut sta, &eth_frame[..])
            .expect_err("Ethernet frame is dropped when client is off channel");
        assert_variant!(error, Error::Status(_str, status) =>
            assert_eq!(status, zx::Status::BAD_STATE),
            "error should contain a status"
        );
    }

    #[test]
    fn assoc_eth_frame_too_short_dropped() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let state =
            States::from(statemachine::testing::new_state(Associated(empty_association(&mut sta))));

        let eth_frame = &[100; 13]; // Needs at least 14 bytes for header.

        let error =
            state.on_eth_frame(&mut sta, &eth_frame[..]).expect_err("Ethernet frame is too short");
        assert_variant!(error, Error::Status(_str, status) =>
            assert_eq!(status, zx::Status::IO_DATA_INTEGRITY),
            "error should contain a status"
        );
    }

    #[test]
    fn assoc_controlled_port_closed_eth_frame_dropped() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let state =
            States::from(statemachine::testing::new_state(Associated(empty_association(&mut sta))));

        let eth_frame = &[100; 14]; // long enough for ethernet header.

        let error = state
            .on_eth_frame(&mut sta, &eth_frame[..])
            .expect_err("Ethernet frame canot be sent when controlled port is closed");
        assert_variant!(error, Error::Status(_str, status) =>
            assert_eq!(status, zx::Status::BAD_STATE),
            "Error should contain status"
        );
    }

    #[test]
    fn not_assoc_eth_frame_dropped() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let state = States::from(statemachine::testing::new_state(Joined));

        let eth_frame = &[100; 14]; // long enough for ethernet header.

        let error = state
            .on_eth_frame(&mut sta, &eth_frame[..])
            .expect_err("Ethernet frame cannot be sent in Joined state");
        assert_variant !(error, Error::Status(_str, status) =>
            assert_eq!(status, zx::Status::BAD_STATE),
            "Error should contain status"
        );
    }

    #[test]
    #[allow(deprecated)] // For constructing MLME message
    fn finalize_assoc_success() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let state = States::from(statemachine::testing::new_state(Associated(Association {
            ap_ht_op: Some(ie::fake_ht_operation().as_bytes().try_into().unwrap()),
            ap_vht_op: Some(ie::fake_vht_operation().as_bytes().try_into().unwrap()),
            ..empty_association(&mut sta)
        })));

        let cap = fidl_mlme::NegotiatedCapabilities {
            channel: fidl_common::WlanChan {
                primary: 32,
                cbw: fidl_common::Cbw::Cbw40,
                secondary80: 88,
            },
            cap_info: 0x1234,
            rates: vec![125, 126, 127, 128, 129, 130],
            wmm_param: None,
            ht_cap: Some(Box::new(fidl_mlme::HtCapabilities {
                bytes: ie::fake_ht_capabilities().as_bytes().try_into().unwrap(),
            })),
            vht_cap: Some(Box::new(fidl_mlme::VhtCapabilities {
                bytes: ie::fake_vht_capabilities().as_bytes().try_into().unwrap(),
            })),
        };
        let state = state.handle_mlme_msg(
            &mut sta,
            fidl_mlme::MlmeRequestMessage::FinalizeAssociationReq { cap },
        );
        assert_variant!(state, States::Associated(_), "should stay in associated");

        assert_eq!(m.fake_device.assocs.keys().count(), 1);
        assert_eq!(*m.fake_device.assocs.keys().next().unwrap(), BSSID.0);

        let assoc_ctx = m.fake_device.assocs.get(&BSSID.0).unwrap();

        assert_eq!(assoc_ctx.aid, 0);
        assert_eq!(assoc_ctx.phy, banjo_ddk_protocol_wlan_info::WlanPhyType::VHT);
        assert_eq!(assoc_ctx.qos, true);
        assert_eq!(assoc_ctx.rates_cnt, 6);
        assert_eq!(assoc_ctx.rates[..6], [125, 126, 127, 128, 129, 130]);
        assert_eq!(assoc_ctx.cap_info, 0x1234);
        assert!(assoc_ctx.has_ht_cap);
        assert!(assoc_ctx.has_vht_cap);
        assert!(assoc_ctx.has_ht_op);
        assert!(assoc_ctx.has_vht_op);
    }

    #[test]
    #[allow(deprecated)] // Raw MLME messages are deprecated.
    fn joined_sme_deauth() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let state = States::from(statemachine::testing::new_state(Joined));
        let state = state.handle_mlme_msg(&mut sta, fake_mlme_deauth_req());
        assert_variant!(state, States::Joined(_), "Joined should stay in Joined");
        // No MLME message was sent because MLME already deauthenticated.
        m.fake_device
            .next_mlme_msg::<fidl_mlme::DeauthenticateIndication>()
            .expect_err("should be no outgoing message");
    }

    #[test]
    fn authenticating_sme_deauth() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let state = States::from(statemachine::testing::new_state(open_authenticating(&mut sta)));

        let state = state.handle_mlme_msg(&mut sta, fake_mlme_deauth_req());

        // Verify timeout was canceled.
        assert_eq!(sta.ctx.timer.scheduled_events(TimedEvent::Authenticating).len(), 0);
        assert_variant!(state, States::Joined(_), "should transition to Joined");

        // No need to notify SME since it already deauthenticated
        m.fake_device
            .next_mlme_msg::<fidl_mlme::DeauthenticateConfirm>()
            .expect_err("should not see more MLME messages");
    }

    #[test]
    fn authenticated_sme_deauth() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let state = States::from(statemachine::testing::new_state(Authenticated));

        let state = state.handle_mlme_msg(&mut sta, fake_mlme_deauth_req());

        assert_variant!(state, States::Joined(_), "should transition to Joined");

        // Should accept the deauthentication request and send back confirm.
        let deauth_conf = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::DeauthenticateConfirm>()
            .expect("should see deauth conf");
        assert_eq!(deauth_conf, fidl_mlme::DeauthenticateConfirm { peer_sta_address: BSSID.0 });
        m.fake_device
            .next_mlme_msg::<fidl_mlme::DeauthenticateConfirm>()
            .expect_err("should not see more MLME messages");
    }

    #[test]
    fn associating_sme_deauth() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let timeout =
            sta.ctx.timer.schedule_event(zx::Time::after(1.seconds()), TimedEvent::Associating);
        let state = States::from(statemachine::testing::new_state(Associating { timeout }));

        let state = state.handle_mlme_msg(&mut sta, fake_mlme_deauth_req());

        // Verify timeout was canceled.
        assert_variant!(sta.ctx.timer.triggered(&timeout), None);
        assert_variant!(state, States::Joined(_), "should transition to Joined");

        // No need to notify SME since it already deauthenticated
        m.fake_device
            .next_mlme_msg::<fidl_mlme::AssociateConfirm>()
            .expect_err("should not see more MLME messages");
    }

    #[test]
    fn associated_sme_deauth() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let state = States::from(statemachine::testing::new_state(Associated(Association {
            controlled_port_open: true,
            ..empty_association(&mut sta)
        })));
        sta.ctx
            .device
            .configure_assoc(fake_ddk_assoc_ctx())
            .expect("valid assoc ctx should not fail");
        assert_eq!(1, m.fake_device.assocs.len());

        sta.ctx.device.set_eth_link_up().expect("should succeed");
        assert_eq!(crate::device::LinkStatus::UP, m.fake_device.link_status);

        let state = state.handle_mlme_msg(&mut sta, fake_mlme_deauth_req());
        assert_variant!(state, States::Joined(_), "should transition to Joined");

        // Should accept the deauthentication request and send back confirm.
        let deauth_conf = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::DeauthenticateConfirm>()
            .expect("should see deauth conf");
        assert_eq!(deauth_conf, fidl_mlme::DeauthenticateConfirm { peer_sta_address: BSSID.0 });
        m.fake_device
            .next_mlme_msg::<fidl_mlme::DeauthenticateConfirm>()
            .expect_err("should not see more MLME messages");
        // Verify association context cleared.
        assert_eq!(0, m.fake_device.assocs.len());
        // Verify ethernet link status is down.
        assert_eq!(crate::device::LinkStatus::DOWN, m.fake_device.link_status);
    }

    #[allow(deprecated)]
    fn fake_mlme_eapol_req() -> fidl_mlme::MlmeRequestMessage {
        fidl_mlme::MlmeRequestMessage::EapolReq {
            req: fidl_mlme::EapolRequest {
                dst_addr: BSSID.0,
                src_addr: IFACE_MAC,
                data: vec![1, 2, 3, 4],
            },
        }
    }

    #[test]
    #[allow(deprecated)]
    fn mlme_eapol_not_associated() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_protected_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);

        let state = States::from(statemachine::testing::new_state(Joined));
        let _state = state.handle_mlme_msg(&mut sta, fake_mlme_eapol_req());
        assert_eq!(m.fake_device.wlan_queue.len(), 0);

        let state = States::from(statemachine::testing::new_state(open_authenticating(&mut sta)));
        m.fake_device.wlan_queue.clear();
        let _state = state.handle_mlme_msg(&mut sta, fake_mlme_eapol_req());
        assert_eq!(m.fake_device.wlan_queue.len(), 0);

        let state = States::from(statemachine::testing::new_state(Authenticated));
        let _state = state.handle_mlme_msg(&mut sta, fake_mlme_eapol_req());
        assert_eq!(m.fake_device.wlan_queue.len(), 0);

        let state = States::from(statemachine::testing::new_state(Associating {
            timeout: EventId::default(),
        }));
        let _state = state.handle_mlme_msg(&mut sta, fake_mlme_eapol_req());
        assert_eq!(m.fake_device.wlan_queue.len(), 0);
    }

    #[test]
    #[allow(deprecated)]
    fn mlme_eapol_associated_not_protected() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);

        let state =
            States::from(statemachine::testing::new_state(Associated(empty_association(&mut sta))));
        let _state = state.handle_mlme_msg(&mut sta, fake_mlme_eapol_req());
        assert_eq!(m.fake_device.wlan_queue.len(), 0);
    }

    #[test]
    #[allow(deprecated)]
    fn mlme_eapol_associated() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_protected_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);

        let state =
            States::from(statemachine::testing::new_state(Associated(empty_association(&mut sta))));
        let _state = state.handle_mlme_msg(&mut sta, fake_mlme_eapol_req());
        assert_eq!(m.fake_device.wlan_queue.len(), 1);
        assert_eq!(
            &m.fake_device.wlan_queue[0].0[..],
            &[
                // Data header (EAPoL frames are data frames)
                0b00001000, 0b00000001, // Frame Control
                0, 0, // Duration
                6, 6, 6, 6, 6, 6, // addr1 - BSSID
                3, 3, 3, 3, 3, 3, // addr2 - IFACE_MAC
                6, 6, 6, 6, 6, 6, // addr3 - BSSID
                0x10, 0, // Sequence Control
                // LLC header
                0xAA, 0xAA, 0x03, // DSAP, SSAP, Control, OUI
                0, 0, 0, // OUI
                0x88, 0x8E, // Protocol ID (EAPoL is 0x888E)
                1, 2, 3, 4, // Payload
            ][..]
        );
    }

    #[allow(deprecated)]
    fn fake_mlme_set_keys_req() -> fidl_mlme::MlmeRequestMessage {
        fidl_mlme::MlmeRequestMessage::SetKeysReq {
            req: fidl_mlme::SetKeysRequest {
                keylist: vec![fidl_mlme::SetKeyDescriptor {
                    cipher_suite_oui: [1, 2, 3],
                    cipher_suite_type: 4,
                    key_type: fidl_mlme::KeyType::Pairwise,
                    address: [5; 6],
                    key_id: 6,
                    key: vec![1, 2, 3, 4, 5, 6, 7],
                    rsc: 8,
                }],
            },
        }
    }

    #[test]
    #[allow(deprecated)]
    fn mlme_set_keys_not_associated() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_protected_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);

        let state = States::from(statemachine::testing::new_state(Joined));
        let _state = state.handle_mlme_msg(&mut sta, fake_mlme_set_keys_req());
        assert_eq!(m.fake_device.keys.len(), 0);

        let state = States::from(statemachine::testing::new_state(open_authenticating(&mut sta)));
        let _state = state.handle_mlme_msg(&mut sta, fake_mlme_set_keys_req());
        assert_eq!(m.fake_device.keys.len(), 0);

        let state = States::from(statemachine::testing::new_state(Authenticated));
        let _state = state.handle_mlme_msg(&mut sta, fake_mlme_set_keys_req());
        assert_eq!(m.fake_device.keys.len(), 0);

        let state = States::from(statemachine::testing::new_state(Associating {
            timeout: EventId::default(),
        }));
        let _state = state.handle_mlme_msg(&mut sta, fake_mlme_set_keys_req());
        assert_eq!(m.fake_device.keys.len(), 0);
    }

    #[test]
    #[allow(deprecated)]
    fn mlme_set_keys_associated_not_protected() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);

        let state =
            States::from(statemachine::testing::new_state(Associated(empty_association(&mut sta))));
        let _state = state.handle_mlme_msg(&mut sta, fake_mlme_set_keys_req());
        assert_eq!(m.fake_device.keys.len(), 0);
    }

    #[test]
    #[allow(deprecated)]
    fn mlme_set_keys_associated() {
        use crate::key::*;
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_protected_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);

        let state =
            States::from(statemachine::testing::new_state(Associated(empty_association(&mut sta))));
        let _state = state.handle_mlme_msg(&mut sta, fake_mlme_set_keys_req());
        assert_eq!(m.fake_device.keys.len(), 1);

        assert_eq!(
            m.fake_device.keys[0],
            KeyConfig {
                bssid: 0,
                protection: Protection::RX_TX,
                cipher_oui: [1, 2, 3],
                cipher_type: 4,
                key_type: KeyType::PAIRWISE,
                peer_addr: [5; 6],
                key_idx: 6,
                key_len: 7,
                key: [
                    1, 2, 3, 4, 5, 6, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                    0, 0, 0, 0, 0, 0,
                ],
                rsc: 8,
            }
        );
    }

    #[allow(deprecated)]
    fn fake_mlme_set_ctrl_port_open(open: bool) -> fidl_mlme::MlmeRequestMessage {
        fidl_mlme::MlmeRequestMessage::SetControlledPort {
            req: fidl_mlme::SetControlledPortRequest {
                peer_sta_address: BSSID.0,
                state: match open {
                    true => fidl_mlme::ControlledPortState::Open,
                    false => fidl_mlme::ControlledPortState::Closed,
                },
            },
        }
    }

    #[test]
    #[allow(deprecated)]
    fn mlme_set_controlled_port_not_associated() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_protected_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);

        let state = States::from(statemachine::testing::new_state(Joined));
        let _state = state.handle_mlme_msg(&mut sta, fake_mlme_set_ctrl_port_open(true));
        assert_eq!(m.fake_device.link_status, crate::device::LinkStatus::DOWN);

        let state = States::from(statemachine::testing::new_state(open_authenticating(&mut sta)));
        let _state = state.handle_mlme_msg(&mut sta, fake_mlme_set_ctrl_port_open(true));
        assert_eq!(m.fake_device.link_status, crate::device::LinkStatus::DOWN);

        let state = States::from(statemachine::testing::new_state(Authenticated));
        let _state = state.handle_mlme_msg(&mut sta, fake_mlme_set_ctrl_port_open(true));
        assert_eq!(m.fake_device.link_status, crate::device::LinkStatus::DOWN);

        let state = States::from(statemachine::testing::new_state(Associating {
            timeout: EventId::default(),
        }));
        let _state = state.handle_mlme_msg(&mut sta, fake_mlme_set_ctrl_port_open(true));
        assert_eq!(m.fake_device.link_status, crate::device::LinkStatus::DOWN);
    }

    #[test]
    #[allow(deprecated)]
    fn mlme_set_controlled_port_associated_not_protected() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);

        let state =
            States::from(statemachine::testing::new_state(Associated(empty_association(&mut sta))));
        let _state = state.handle_mlme_msg(&mut sta, fake_mlme_set_ctrl_port_open(true));
        assert_eq!(m.fake_device.link_status, crate::device::LinkStatus::DOWN);
    }

    #[test]
    #[allow(deprecated)]
    fn mlme_set_controlled_port_associated() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_protected_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);

        let state =
            States::from(statemachine::testing::new_state(Associated(empty_association(&mut sta))));
        assert_eq!(m.fake_device.link_status, crate::device::LinkStatus::DOWN);
        let state = state.handle_mlme_msg(&mut sta, fake_mlme_set_ctrl_port_open(true));
        assert_eq!(m.fake_device.link_status, crate::device::LinkStatus::UP);
        let _state = state.handle_mlme_msg(&mut sta, fake_mlme_set_ctrl_port_open(false));
        assert_eq!(m.fake_device.link_status, crate::device::LinkStatus::DOWN);
    }

    #[test]
    fn associated_request_bu_if_tim_indicates_buffered_frame() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let state = States::from(statemachine::testing::new_state(Associated(Association {
            aid: 1,
            ..empty_association(&mut sta)
        })));

        let beacon = [
            // Mgmt header
            0b10000000, 0, // Frame Control
            0, 0, // Duration
            3, 3, 3, 3, 3, 3, // Addr1 == IFACE_MAC
            7, 7, 7, 7, 7, 7, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0, 0, // Sequence Control
            // Beacon header:
            0, 0, 0, 0, 0, 0, 0, 0, // Timestamp
            10, 0, // Beacon interval
            33, 0, // Capabilities
            5, 4, 0, 0, 0, 0b00000010, // Tim IE: bit 1 in the last octet indicates AID 1
        ];

        state.on_mac_frame(&mut sta, &beacon[..], None);

        assert_eq!(m.fake_device.wlan_queue.len(), 1);
        assert_eq!(
            &m.fake_device.wlan_queue[0].0[..],
            &[
                0b10100100, 0, // Frame control
                1, 0b11000000, // ID (2 MSBs are set to 1 from the AID)
                6, 6, 6, 6, 6, 6, // BSSID
                3, 3, 3, 3, 3, 3, // TA
            ][..]
        );
    }

    #[test]
    fn associated_does_not_request_bu_if_tim_indicates_no_buffered_frame() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);
        let state = States::from(statemachine::testing::new_state(Associated(Association {
            aid: 1,
            ..empty_association(&mut sta)
        })));

        let beacon = [
            // Mgmt header
            0b10000000, 0, // Frame Control
            0, 0, // Duration
            3, 3, 3, 3, 3, 3, // Addr1 == IFACE_MAC
            7, 7, 7, 7, 7, 7, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0, 0, // Sequence Control
            // Beacon header:
            0, 0, 0, 0, 0, 0, 0, 0, // Timestamp
            10, 0, // Beacon interval
            33, 0, // Capabilities
            5, 4, 0, 0, 0, 0, // Tim IE: No buffered frame for any client.
        ];
        state.on_mac_frame(&mut sta, &beacon[..], None);

        assert_eq!(m.fake_device.wlan_queue.len(), 0);
    }

    fn rx_info_with_dbm(rssi_dbm: i8) -> banjo_wlan_mac::WlanRxInfo {
        banjo_wlan_mac::WlanRxInfo {
            rx_flags: 0,
            valid_fields: banjo_wlan_info::WlanRxInfoValid::RSSI.0,
            phy: 0,
            data_rate: 0,
            chan: banjo_wlan_info::WlanChannel {
                primary: 0,
                cbw: banjo_wlan_info::WlanChannelBandwidth::_20,
                secondary80: 0,
            },
            mcs: 0,
            rssi_dbm,
            rcpi_dbmh: 0,
            snr_dbh: 0,
        }
    }

    #[test]
    fn signal_report() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_protected_client_station();
        let mut sta = sta.bind(&mut ctx, &mut m.scanner, &mut m.chan_sched, &mut m.channel_state);

        let state = States::from(State::from(statemachine::testing::new_state(Associated(
            empty_association(&mut sta),
        ))));

        let (id, _dealine) =
            m.fake_scheduler.next_event().expect("should see a signal report timeout");
        let event = sta.ctx.timer.triggered(&id).expect("event id should exist");
        let state = state.on_timed_event(&mut sta, event, id);

        let signal_ind = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::SignalReportIndication>()
            .expect("should see a signal report");

        // -128 is the default value, equivalent to 0 watt.
        assert_eq!(signal_ind.rssi_dbm, -128);

        let beacon = [
            // Mgmt header
            0b10000000, 0, // Frame Control
            0, 0, // Duration
            3, 3, 3, 3, 3, 3, // Addr1 == IFACE_MAC
            7, 7, 7, 7, 7, 7, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0, 0, // Sequence Control
            // Beacon header:
            0, 0, 0, 0, 0, 0, 0, 0, // Timestamp
            10, 0, // Beacon interval
            33, 0, // Capabilities
        ];

        const EXPECTED_DBM: i8 = -32;
        let state = state.on_mac_frame(&mut sta, &beacon[..], Some(rx_info_with_dbm(EXPECTED_DBM)));

        let (id, _dealine) =
            m.fake_scheduler.next_event().expect("should see a signal report timeout");
        let event = sta.ctx.timer.triggered(&id).expect("event id should exist");
        let _state = state.on_timed_event(&mut sta, event, id);

        let signal_ind = m
            .fake_device
            .next_mlme_msg::<fidl_mlme::SignalReportIndication>()
            .expect("should see a signal report");

        assert_eq!(signal_ind.rssi_dbm, EXPECTED_DBM);
    }
}
