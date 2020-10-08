// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        ap::{
            aid, authenticator::Authenticator, event::*, remote_client::RemoteClient, Context,
            RsnCfg,
        },
        timer::EventId,
    },
    anyhow::{ensure, format_err},
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_mlme as fidl_mlme,
    fuchsia_zircon::{self as zx, DurationNum},
    log::error,
    std::sync::{Arc, Mutex},
    wlan_common::{
        ie::{intersect, rsn::rsne, SupportedRate},
        mac::{Aid, CapabilityInfo, MacAddr},
    },
    wlan_rsn::{
        self,
        gtk::GtkProvider,
        nonce::NonceReader,
        rsna::{SecAssocStatus, SecAssocUpdate, UpdateSink},
        NegotiatedProtection, ProtectionInfo,
    },
    wlan_statemachine::*,
    zerocopy::AsBytes,
};

// This is not specified by 802.11, but we need some way of kicking out clients that authenticate
// but don't intend to associate.
const ASSOCIATION_TIMEOUT_SECONDS: i64 = 300;

/// Authenticating is the initial state a client is in when it arrives at the SME.
///
/// It may proceed to Authenticated if an appropriate MLME-AUTHENTICATE.indication is received.
///
/// If a client had previously been in an authenticated state (i.e. Authenticated or Associated) and
/// is no longer, it must be forgotten from the SME's known clients.
pub struct Authenticating;

impl Authenticating {
    /// Handles MLME-AUTHENTICATE.indication.
    ///
    /// Currently, only open system authentication is supported.
    ///
    /// If authentication succeeds, an event ID for association timeout is returned and the client
    /// state machine may proceed to Associated. Otherwise, an error is returned and the client
    /// should be forgotten from the SME.
    fn handle_auth_ind(
        &self,
        r_sta: &mut RemoteClient,
        ctx: &mut Context,
        auth_type: fidl_mlme::AuthenticationTypes,
    ) -> Result<EventId, anyhow::Error> {
        // We only support open system authentication today.
        if auth_type != fidl_mlme::AuthenticationTypes::OpenSystem {
            return Err(format_err!("unsupported authentication type: {:?}", auth_type));
        }

        let event = ClientEvent::AssociationTimeout;
        let timeout_event_id =
            r_sta.schedule_at(ctx, zx::Time::after(ASSOCIATION_TIMEOUT_SECONDS.seconds()), event);

        Ok(timeout_event_id)
    }
}

/// Creates a new WPA2-PSK CCMP-128 authenticator.
fn new_authenticator_from_rsne(
    device_addr: MacAddr,
    client_addr: MacAddr,
    s_rsne_bytes: &[u8],
    a_rsn: &RsnCfg,
) -> Result<Box<dyn Authenticator>, anyhow::Error> {
    let (_, s_rsne) =
        rsne::from_bytes(s_rsne_bytes).map_err(|e| format_err!("failed to parse RSNE: {:?}", e))?;
    ensure!(s_rsne.is_valid_subset_of(&a_rsn.rsne)?, "incompatible client RSNE");

    let nonce_reader = NonceReader::new(&device_addr)?;
    let gtk_provider = GtkProvider::new(NegotiatedProtection::from_rsne(&s_rsne)?.group_data)?;

    Ok(Box::new(wlan_rsn::Authenticator::new_wpa2psk_ccmp128(
        // Note: There should be one Reader per device, not per SME.
        // Follow-up with improving on this.
        nonce_reader,
        Arc::new(Mutex::new(gtk_provider)),
        a_rsn.psk.clone(),
        client_addr,
        ProtectionInfo::Rsne(s_rsne),
        device_addr,
        ProtectionInfo::Rsne(a_rsn.rsne.clone()),
    )?))
}

/// Authenticated is the state a client is in when the SME has successfully accepted an
/// MLME-AUTHENTICATE.indication.
///
/// While the client is Authenticated, a timeout event will fire to transition it back to
/// Authenticating if it has not associated in time.
pub struct Authenticated {
    timeout_event_id: EventId,
}

/// AssociationError holds an error to log and the result code to send to the MLME for the
/// association rejection.
struct AssociationError {
    error: anyhow::Error,
    result_code: fidl_mlme::AssociateResultCodes,
    reason_code: fidl_mlme::ReasonCode,
}

/// Contains information from a successful association.
struct Association {
    aid: Aid,
    capabilities: CapabilityInfo,
    rates: Vec<u8>,
    rsna_link_state: Option<RsnaLinkState>,
}

impl Authenticated {
    /// Handles an association indication.
    ///
    /// It will:
    /// - assign an association ID from the provided indication map.
    /// - find common rates between the client and the AP.
    /// - if the AP has an RSN configuration, and the client has provided a supplicant RSNE,
    ///   negotiate an EAPoL controlled port.
    ///
    /// If unsuccessful, the resulting error will indicate the MLME result code.
    fn handle_assoc_ind(
        &self,
        r_sta: &mut RemoteClient,
        ctx: &mut Context,
        aid_map: &mut aid::Map,
        ap_capabilities: CapabilityInfo,
        client_capablities: u16,
        ap_rates: &[SupportedRate],
        client_rates: &[u8],
        rsn_cfg: &Option<RsnCfg>,
        s_rsne: Option<Vec<u8>>,
    ) -> Result<Association, AssociationError> {
        let rsna_link_state = match (s_rsne.as_ref(), rsn_cfg) {
            (Some(s_rsne_bytes), Some(a_rsn)) => {
                let authenticator = new_authenticator_from_rsne(
                    ctx.device_info.mac_addr,
                    r_sta.addr,
                    s_rsne_bytes,
                    a_rsn,
                )
                .map_err(|error| AssociationError {
                    error,
                    result_code: fidl_mlme::AssociateResultCodes::RefusedCapabilitiesMismatch,
                    reason_code: fidl_mlme::ReasonCode::Ieee8021XAuthFailed,
                })?;

                Some(RsnaLinkState::new(authenticator))
            }
            (None, None) => None,
            _ => {
                return Err(AssociationError {
                    error: format_err!("unexpected RSN element: {:?}", s_rsne),
                    result_code: fidl_mlme::AssociateResultCodes::RefusedCapabilitiesMismatch,
                    reason_code: fidl_mlme::ReasonCode::ReasonInvalidElement,
                });
            }
        };

        let aid = aid_map.assign_aid().map_err(|error| AssociationError {
            error,
            result_code: fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified,
            reason_code: fidl_mlme::ReasonCode::UnspecifiedReason,
        })?;

        let (capabilities, rates) =
            if ctx.device_info.driver_features.contains(&fidl_common::DriverFeature::TempSoftmac) {
                let capabilities = CapabilityInfo(client_capablities & ap_capabilities.raw());

                // The IEEE 802.11 standard doesn't really specify what happens if the client rates
                // mismatch the AP rates at this point: the client should have already determined
                // the appropriate rates via the beacon or probe response frames. However, just to
                // be safe, we intersect these rates here.
                let rates =
                    intersect::intersect_rates(intersect::ApRates(ap_rates), client_rates.into())
                        .map_err(|error| AssociationError {
                            error: format_err!(
                                "could not intersect rates ({:?} + {:?}): {:?}",
                                ap_rates,
                                client_rates,
                                error
                            ),
                            result_code: match error {
                                intersect::IntersectRatesError::BasicRatesMismatch => {
                                    fidl_mlme::AssociateResultCodes::RefusedBasicRatesMismatch
                                }
                                intersect::IntersectRatesError::NoApRatesSupported => {
                                    fidl_mlme::AssociateResultCodes::RefusedCapabilitiesMismatch
                                }
                            },
                            reason_code: fidl_mlme::ReasonCode::ReasonInvalidElement,
                        })?
                        .as_bytes()
                        .to_vec();

                (capabilities, rates)
            } else {
                // If we are using a FullMAC driver, don't do the intersection and just pass the
                // client rates back: they won't be used meaningfully anyway.
                (CapabilityInfo(client_capablities), client_rates.to_vec())
            };

        Ok(Association {
            capabilities: capabilities
                // IEEE Std 802.11-2016, 9.4.1.4: An AP sets the Privacy subfield to 1 within
                // transmitted Beacon, Probe Response, (Re)Association Response frames if data
                // confidentiality is required for all Data frames exchanged within the BSS.
                .with_privacy(rsna_link_state.is_some()),
            aid,
            rates,
            rsna_link_state,
        })
    }

    /// Handles incoming association timeout events.
    ///
    /// The result determines whether or not we remain in this state or not:
    /// - if the result is Ok, the client is remaining in Authenticated state.
    /// - if the result is Err, the client is deauthenticating due an incoming timeout that is
    ///   indicates that the client has not associated before timing out.
    fn handle_association_timeout(
        &self,
        _unused_r_sta: &mut RemoteClient,
        _unused_ctx: &mut Context,
        timeout_event_id: EventId,
    ) -> Result<(), ()> {
        if timeout_event_id != self.timeout_event_id {
            // This is not the timeout we scheduled earlier, so just ignore it.
            return Ok(());
        }
        Err(())
    }
}

/// RsnaLinkState contains the link state for 802.1X EAP authentication, if RSN configuration is
/// present.
#[derive(Debug)]
struct RsnaLinkState {
    authenticator: Box<dyn Authenticator>,

    /// The last key frame may be replayed up to RSNA_NEGOTIATION_REQUEST_MAX_ATTEMPTS times, so
    /// we hold onto it here.
    last_key_frame: Option<eapol::KeyFrameBuf>,

    request_attempts: usize,
    request_timeout_event_id: Option<EventId>,
    negotiation_timeout_event_id: Option<EventId>,
}

pub const RSNA_NEGOTIATION_REQUEST_MAX_ATTEMPTS: usize = 4;
pub const RSNA_NEGOTIATION_REQUEST_TIMEOUT_SECONDS: i64 = 1;
pub const RSNA_NEGOTIATION_TIMEOUT_SECONDS: i64 = 5;

impl RsnaLinkState {
    fn new(authenticator: Box<dyn Authenticator>) -> Self {
        Self {
            authenticator,
            last_key_frame: None,
            request_attempts: 0,
            request_timeout_event_id: None,
            negotiation_timeout_event_id: None,
        }
    }

    /// Initiates a key exchange between the remote client and AP.
    ///
    /// It will also set a key exchange timeout.
    fn initiate_key_exchange(
        &mut self,
        r_sta: &mut RemoteClient,
        ctx: &mut Context,
    ) -> Result<(), anyhow::Error> {
        let mut update_sink = vec![];
        self.authenticator.initiate(&mut update_sink)?;
        self.process_authenticator_updates(r_sta, ctx, &update_sink);

        if self.last_key_frame.is_none() {
            return Err(format_err!("no key frame was produced on authenticator initiation"));
        }

        self.negotiation_timeout_event_id.replace(r_sta.schedule_at(
            ctx,
            zx::Time::after(zx::Duration::from_seconds(RSNA_NEGOTIATION_TIMEOUT_SECONDS)),
            ClientEvent::RsnaTimeout(RsnaTimeout::Negotiation),
        ));

        self.reschedule_request_timeout(r_sta, ctx);
        Ok(())
    }

    fn reschedule_request_timeout(&mut self, r_sta: &mut RemoteClient, ctx: &mut Context) {
        self.request_timeout_event_id.replace(r_sta.schedule_at(
            ctx,
            zx::Time::after(zx::Duration::from_seconds(RSNA_NEGOTIATION_REQUEST_TIMEOUT_SECONDS)),
            ClientEvent::RsnaTimeout(RsnaTimeout::Request),
        ));
    }

    fn handle_rsna_timeout(
        &mut self,
        r_sta: &mut RemoteClient,
        ctx: &mut Context,
        timeout_event_id: EventId,
        timeout_type: RsnaTimeout,
    ) -> Result<(), RsnaNegotiationError> {
        match timeout_type {
            RsnaTimeout::Request => self.handle_rsna_request_timeout(r_sta, ctx, timeout_event_id),
            RsnaTimeout::Negotiation => self.handle_rsna_negotiation_timeout(timeout_event_id),
        }
    }

    fn handle_rsna_request_timeout(
        &mut self,
        r_sta: &mut RemoteClient,
        ctx: &mut Context,
        timeout_event_id: EventId,
    ) -> Result<(), RsnaNegotiationError> {
        if self.request_timeout_event_id != Some(timeout_event_id) {
            // This was not the timeout we scheduled earlier.
            return Ok(());
        }
        self.request_timeout_event_id = None;

        self.request_attempts += 1;
        if self.request_attempts >= RSNA_NEGOTIATION_REQUEST_MAX_ATTEMPTS {
            return Err(RsnaNegotiationError::Timeout);
        }

        let frame = self
            .last_key_frame
            .as_ref()
            .ok_or(RsnaNegotiationError::Error(format_err!("no key frame available to resend?")))?;

        r_sta.send_eapol_req(ctx, frame.clone());
        self.reschedule_request_timeout(r_sta, ctx);
        Ok(())
    }

    fn handle_rsna_negotiation_timeout(
        &mut self,
        timeout_event_id: EventId,
    ) -> Result<(), RsnaNegotiationError> {
        if self.negotiation_timeout_event_id != Some(timeout_event_id) {
            // This was not the timeout we scheduled earlier.
            return Ok(());
        }
        self.negotiation_timeout_event_id = None;
        Err(RsnaNegotiationError::Timeout)
    }

    /// Processes updates from the authenticator.
    fn process_authenticator_updates(
        &mut self,
        r_sta: &mut RemoteClient,
        ctx: &mut Context,
        update_sink: &UpdateSink,
    ) {
        for update in update_sink {
            match update {
                SecAssocUpdate::TxEapolKeyFrame(frame) => {
                    r_sta.send_eapol_req(ctx, frame.clone());
                    self.last_key_frame = Some(frame.clone());
                }
                SecAssocUpdate::Key(key) => r_sta.send_key(ctx, &key),
                SecAssocUpdate::Status(status) => match status {
                    SecAssocStatus::EssSaEstablished => {
                        r_sta.send_set_controlled_port_req(
                            ctx,
                            fidl_mlme::ControlledPortState::Open,
                        );

                        // Negotiation is complete, clear the timeout and stop storing the last key
                        // frame.
                        self.last_key_frame = None;
                        self.request_timeout_event_id = None;
                        self.negotiation_timeout_event_id = None;
                    }
                    _ => (),
                },
                update => error!("Unhandled association update: {:?}", update),
            }
        }
    }

    /// Passes EAPoL frames into the underlying authenticator.
    fn handle_eapol_frame(
        &mut self,
        r_sta: &mut RemoteClient,
        ctx: &mut Context,
        data: &[u8],
    ) -> Result<(), anyhow::Error> {
        self.request_attempts = 0;

        let authenticator = self.authenticator.as_mut();
        let key_frame = eapol::KeyFrameRx::parse(
            authenticator.get_negotiated_protection().mic_size as usize,
            data,
        )?;

        let mut update_sink = vec![];
        authenticator.on_eapol_frame(&mut update_sink, eapol::Frame::Key(key_frame))?;
        self.process_authenticator_updates(r_sta, ctx, &update_sink);
        Ok(())
    }
}

/// Authenticated is the state a client is in when the SME has successfully accepted an
/// MLME-ASSOCIATE.indication.
pub struct Associated {
    aid: Aid,
    rsna_link_state: Option<RsnaLinkState>,
}

enum RsnaNegotiationError {
    Error(anyhow::Error),
    Timeout,
}

impl Associated {
    fn aid(&self) -> Aid {
        self.aid
    }

    /// If RSNA configuration is present, handles per-request (i.e. key frame resend) or negotiation
    /// timeouts.
    fn handle_rsna_timeout(
        &mut self,
        r_sta: &mut RemoteClient,
        ctx: &mut Context,
        timeout_event_id: EventId,
        timeout_type: RsnaTimeout,
    ) -> Result<(), RsnaNegotiationError> {
        match self.rsna_link_state.as_mut() {
            Some(rsna_link_state) => {
                rsna_link_state.handle_rsna_timeout(r_sta, ctx, timeout_event_id, timeout_type)
            }
            None => Ok(()),
        }
    }

    /// If RSNA configuration is present, forwards EAPoL frames to the authenticator. Otherwise,
    /// returns an error.
    fn handle_eapol_ind(
        &mut self,
        r_sta: &mut RemoteClient,
        ctx: &mut Context,
        data: &[u8],
    ) -> Result<(), anyhow::Error> {
        match self.rsna_link_state.as_mut() {
            Some(rsna_link_state) => rsna_link_state.handle_eapol_frame(r_sta, ctx, data),
            None => {
                return Err(format_err!("received EAPoL indication without RSNA link state"));
            }
        }
    }

    /// Handles an incoming disassociation from the client. An event ID for association timeout is
    /// returned and the client state machine may proceed to Associated.
    fn handle_disassoc_ind(
        &self,
        r_sta: &mut RemoteClient,
        ctx: &mut Context,
        aid_map: &mut aid::Map,
    ) -> EventId {
        aid_map.release_aid(self.aid);
        let event = ClientEvent::AssociationTimeout;
        r_sta.schedule_at(ctx, zx::Time::after(ASSOCIATION_TIMEOUT_SECONDS.seconds()), event)
    }
}

statemachine!(
    pub enum States,

    () => Authenticating,
    Authenticating => Authenticated,
    Authenticated => Associated,

    Associated => Authenticated,
    Authenticated => Authenticating,

    // Allow associated to go directly to authenticating, if we fail RSN authentication.
    Associated => Authenticating,
);

/// The external representation of the state machine for the client.
impl States {
    pub fn new_initial() -> States {
        States::from(State::new(Authenticating))
    }

    /// Retrieves the association ID of the remote client.
    ///
    /// aid() != None iff the client is associated.
    pub fn aid(&self) -> Option<Aid> {
        match self {
            States::Associated(state) => Some(state.aid()),
            _ => None,
        }
    }

    /// Returns if the client is (at least) authenticated (i.e. authenticated or associated).
    pub fn authenticated(&self) -> bool {
        match self {
            States::Authenticating(..) => false,
            _ => true,
        }
    }

    /// Handles an incoming MLME-AUTHENTICATE.indication.
    ///
    /// On success, sends a successful MLME-AUTHENTICATE.response and transitions the client to
    /// Authenticated.
    ///
    /// Otherwise, sends a refused MLME-AUTHENTICATE.response and leaves the client in
    /// Authenticating. The caller should forget this client from its internal state.
    pub fn handle_auth_ind(
        self,
        r_sta: &mut RemoteClient,
        ctx: &mut Context,
        auth_type: fidl_mlme::AuthenticationTypes,
    ) -> States {
        match self {
            States::Authenticating(state) => {
                match state.handle_auth_ind(r_sta, ctx, auth_type).into() {
                    Ok(timeout_event_id) => {
                        r_sta.send_authenticate_resp(
                            ctx,
                            fidl_mlme::AuthenticateResultCodes::Success,
                        );
                        state.transition_to(Authenticated { timeout_event_id }).into()
                    }
                    Err(e) => {
                        error!("client {:02X?} MLME-AUTHENTICATE.indication: {}", r_sta.addr, e);
                        r_sta.send_authenticate_resp(
                            ctx,
                            fidl_mlme::AuthenticateResultCodes::Refused,
                        );
                        state.into()
                    }
                }
            }
            _ => {
                r_sta.send_authenticate_resp(ctx, fidl_mlme::AuthenticateResultCodes::Refused);
                self
            }
        }
    }

    /// Handles an incoming MLME-ASSOCIATE.indication.
    ///
    /// On success, sends a successful MLME-ASSOCIATE.response and transitions the client to
    /// Authenticated.
    ///
    /// Otherwise, sends an unsuccessful MLME-ASSOCIATE.response AND a MLME-DEAUTHENTICATE-request,
    /// and transitions the client to Authenticating. The caller should forget this client from its
    /// internal state.
    pub fn handle_assoc_ind(
        self,
        r_sta: &mut RemoteClient,
        ctx: &mut Context,
        aid_map: &mut aid::Map,
        ap_capabilities: CapabilityInfo,
        client_capablities: u16,
        ap_rates: &[SupportedRate],
        client_rates: &[u8],
        rsn_cfg: &Option<RsnCfg>,
        s_rsne: Option<Vec<u8>>,
    ) -> States {
        match self {
            States::Authenticated(state) => {
                match state.handle_assoc_ind(
                    r_sta,
                    ctx,
                    aid_map,
                    ap_capabilities,
                    client_capablities,
                    ap_rates,
                    client_rates,
                    rsn_cfg,
                    s_rsne,
                ) {
                    Ok(Association { aid, capabilities, rates, mut rsna_link_state }) => {
                        r_sta.send_associate_resp(
                            ctx,
                            fidl_mlme::AssociateResultCodes::Success,
                            aid,
                            capabilities,
                            rates,
                        );

                        // RSNA authentication needs to be handled after association.
                        if let Some(rsna_link_state) = rsna_link_state.as_mut() {
                            if let Err(error) = rsna_link_state.initiate_key_exchange(r_sta, ctx) {
                                error!(
                                    "client {:02X?} MLME-ASSOCIATE.indication (key exchange): {}",
                                    r_sta.addr, error
                                );
                                r_sta.send_deauthenticate_req(
                                    ctx,
                                    fidl_mlme::ReasonCode::Ieee8021XAuthFailed,
                                );
                                return state.transition_to(Authenticating).into();
                            }
                        }

                        state.transition_to(Associated { aid, rsna_link_state }).into()
                    }
                    Err(AssociationError { error, result_code, reason_code }) => {
                        error!("client {:02X?} MLME-ASSOCIATE.indication: {}", r_sta.addr, error);
                        r_sta.send_associate_resp(ctx, result_code, 0, CapabilityInfo(0), vec![]);
                        r_sta.send_deauthenticate_req(ctx, reason_code);
                        state.transition_to(Authenticating).into()
                    }
                }
            }
            _ => {
                r_sta.send_associate_resp(
                    ctx,
                    fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified,
                    0,
                    CapabilityInfo(0),
                    vec![],
                );
                self
            }
        }
    }

    /// Handles an incoming MLME-DISASSOCIATE.indication.
    ///
    /// Unconditionally transitions the client to Authenticated.
    pub fn handle_disassoc_ind(
        self,
        r_sta: &mut RemoteClient,
        ctx: &mut Context,
        aid_map: &mut aid::Map,
    ) -> States {
        match self {
            States::Associated(state) => {
                let timeout_event_id = state.handle_disassoc_ind(r_sta, ctx, aid_map);
                state.transition_to(Authenticated { timeout_event_id }).into()
            }
            _ => self,
        }
    }

    /// Handles an incoming EAPOL.indication.
    ///
    /// This may update the client's RSNA link state. This will not transition the client.
    pub fn handle_eapol_ind(
        self,
        r_sta: &mut RemoteClient,
        ctx: &mut Context,
        data: &[u8],
    ) -> States {
        match self {
            States::Associated(mut state) => {
                if let Err(e) = state.handle_eapol_ind(r_sta, ctx, data) {
                    error!("client {:02X?} EAPOL.indication: {}", r_sta.addr, e);
                }
                state.into()
            }
            _ => self,
        }
    }

    /// Handles a timeout.
    ///
    /// If the timeout is not being handled by the underlying state (e.g. if an association timeout
    /// fires but the client has transitioned to Associated), the timeout is ignored.
    ///
    /// If the timeout is a handled association timeout, MLME-DEAUTHENTICATE.request is sent to the
    /// client the client is transitioned to Authenticating. The caller should forget this client
    /// from its internal state.
    ///
    /// If the timeout is a key exchange timeout, the client may either reattempt its key exchange
    /// or otherwise exceed the maximum number of key exchange attempts:
    ///
    /// - If key exchange initiation is successful, no transition occurs.
    ///
    /// - If key exchange fails, MLME-DEAUTHENTICATE.request is sent to the client and the client
    ///   is transitioned to Authenticating. The caller should forget this client from its internal
    ///   state.
    ///
    /// - If the client is out key exchange attempts, MLME-DEAUTHENTICATE.request is sent to the
    ///   client and the client is transitioned to Authenticating. The caller should forget this
    ///   client from its internal state.
    pub fn handle_timeout(
        self,
        r_sta: &mut RemoteClient,
        ctx: &mut Context,
        timeout_event_id: EventId,
        event: ClientEvent,
    ) -> States {
        match event {
            ClientEvent::AssociationTimeout => match self {
                States::Authenticated(state) => {
                    match state.handle_association_timeout(r_sta, ctx, timeout_event_id) {
                        Ok(()) => state.into(),
                        Err(()) => {
                            r_sta.send_deauthenticate_req(
                                ctx,
                                // Not sure if this is the correct reason code.
                                fidl_mlme::ReasonCode::InvalidAuthentication,
                            );
                            state.transition_to(Authenticating).into()
                        }
                    }
                }
                States::Associated(state) => {
                    // If the client is already associated, we can't time it out.
                    state.into()
                }
                _ => {
                    error!(
                        "client {:02X?} received AssociationTimeout in unexpected state; \
                         ignoring timeout",
                        r_sta.addr,
                    );
                    self
                }
            },
            ClientEvent::RsnaTimeout(timeout_type) => match self {
                States::Associated(state) => {
                    let (transition, mut state) = state.release_data();
                    match state.handle_rsna_timeout(r_sta, ctx, timeout_event_id, timeout_type) {
                        Ok(()) => transition.to(state).into(),
                        Err(e) => {
                            let reason_code = match e {
                                RsnaNegotiationError::Error(e) => {
                                    error!(
                                        "client {:02X?} RSNA negotiation error: {}",
                                        r_sta.addr, e
                                    );
                                    fidl_mlme::ReasonCode::UnspecifiedReason
                                }
                                RsnaNegotiationError::Timeout => {
                                    fidl_mlme::ReasonCode::FourwayHandshakeTimeout
                                }
                            };
                            r_sta.send_deauthenticate_req(ctx, reason_code);
                            transition.to(Authenticating).into()
                        }
                    }
                }
                _ => {
                    error!(
                        "client {:02X?} received RsnaTimeout in unexpected state; \
                         ignoring timeout",
                        r_sta.addr,
                    );
                    self
                }
            },
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            ap::{aid, create_rsn_cfg, test_utils::MockAuthenticator, TimeStream},
            sink::MlmeSink,
            test_utils, timer, MlmeRequest, MlmeStream,
        },
        futures::channel::mpsc,
        wlan_common::{
            assert_variant,
            ie::rsn::{
                akm::AKM_PSK,
                cipher::{CIPHER_CCMP_128, CIPHER_GCMP_256},
                rsne::Rsne,
            },
            mac::MacAddr,
        },
        wlan_rsn::key::exchange::Key,
    };

    const AP_ADDR: MacAddr = [6u8; 6];
    const CLIENT_ADDR: MacAddr = [7u8; 6];

    fn make_remote_client() -> RemoteClient {
        RemoteClient::new(CLIENT_ADDR)
    }

    fn make_env() -> (Context, MlmeStream, TimeStream) {
        let device_info = test_utils::fake_device_info(AP_ADDR);
        let (mlme_sink, mlme_stream) = mpsc::unbounded();
        let (timer, time_stream) = timer::create_timer();
        let ctx = Context { device_info, mlme_sink: MlmeSink::new(mlme_sink), timer };
        (ctx, mlme_stream, time_stream)
    }

    #[test]
    fn authenticating_goes_to_authenticated() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, mut time_stream) = make_env();

        let state = States::from(State::new(Authenticating));
        let state =
            state.handle_auth_ind(&mut r_sta, &mut ctx, fidl_mlme::AuthenticationTypes::OpenSystem);

        let (_, Authenticated { timeout_event_id }) = match state {
            States::Authenticated(state) => state.release_data(),
            _ => panic!("unexpected state"),
        };

        let (_, timed_event) = time_stream.try_next().unwrap().expect("expected timed event");
        assert_eq!(timed_event.id, timeout_event_id);
        assert_variant!(timed_event.event, Event::Client { addr, event } => {
            assert_eq!(addr, CLIENT_ADDR);
            assert_variant!(event, ClientEvent::AssociationTimeout);
        });

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::AuthResponse(fidl_mlme::AuthenticateResponse {
            peer_sta_address,
            result_code,
        }) => {
            assert_eq!(peer_sta_address, CLIENT_ADDR);
            assert_eq!(result_code, fidl_mlme::AuthenticateResultCodes::Success);
        });
    }

    #[test]
    fn authenticating_stays_authenticating_with_unsupported_authentication_type() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();

        let state = States::from(State::new(Authenticating));
        let state =
            state.handle_auth_ind(&mut r_sta, &mut ctx, fidl_mlme::AuthenticationTypes::SharedKey);

        let (_, Authenticating) = match state {
            States::Authenticating(state) => state.release_data(),
            _ => panic!("unexpected state"),
        };

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::AuthResponse(fidl_mlme::AuthenticateResponse {
            peer_sta_address,
            result_code,
        }) => {
            assert_eq!(peer_sta_address, CLIENT_ADDR);
            assert_eq!(result_code, fidl_mlme::AuthenticateResultCodes::Refused);
        });
    }

    #[test]
    fn authenticating_refuses_association() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();

        let state = States::from(State::new(Authenticating));

        let mut aid_map = aid::Map::default();
        let state = state.handle_assoc_ind(
            &mut r_sta,
            &mut ctx,
            &mut aid_map,
            CapabilityInfo(0).with_short_preamble(true),
            CapabilityInfo(0).with_short_preamble(true).raw(),
            &[SupportedRate(0b11111000)][..],
            &[0b11111000][..],
            &None,
            None,
        );

        let (_, Authenticating) = match state {
            States::Authenticating(state) => state.release_data(),
            _ => panic!("unexpected state"),
        };

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::AssocResponse(fidl_mlme::AssociateResponse {
            peer_sta_address,
            association_id,
            result_code,
            cap,
            rates,
        }) => {
            assert_eq!(peer_sta_address, CLIENT_ADDR);
            assert_eq!(association_id, 0);
            assert_eq!(result_code, fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified);
            assert_eq!(cap, 0);
            assert_eq!(rates, Vec::<u8>::new());
        });
    }

    #[test]
    fn authenticated_refuses_authentication() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();

        let state: States =
            State::new(Authenticating).transition_to(Authenticated { timeout_event_id: 1 }).into();
        let state =
            state.handle_auth_ind(&mut r_sta, &mut ctx, fidl_mlme::AuthenticationTypes::SharedKey);

        let (_, Authenticated { .. }) = match state {
            States::Authenticated(state) => state.release_data(),
            _ => panic!("unexpected state"),
        };

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::AuthResponse(fidl_mlme::AuthenticateResponse {
            peer_sta_address,
            result_code,
        }) => {
            assert_eq!(peer_sta_address, CLIENT_ADDR);
            assert_eq!(result_code, fidl_mlme::AuthenticateResultCodes::Refused);
        });
    }

    #[test]
    fn authenticated_deauthenticates_on_timeout() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();

        let state: States =
            State::new(Authenticating).transition_to(Authenticated { timeout_event_id: 1 }).into();
        let state = state.handle_timeout(&mut r_sta, &mut ctx, 1, ClientEvent::AssociationTimeout);

        let (_, Authenticating) = match state {
            States::Authenticating(state) => state.release_data(),
            _ => panic!("unexpected state"),
        };

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::Deauthenticate(fidl_mlme::DeauthenticateRequest {
            peer_sta_address,
            reason_code,
        }) => {
            assert_eq!(peer_sta_address, CLIENT_ADDR);
            assert_eq!(reason_code, fidl_mlme::ReasonCode::InvalidAuthentication);
        });
    }

    #[test]
    fn authenticated_does_not_send_deauthentication_on_wrong_timeout() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();

        let state: States =
            State::new(Authenticating).transition_to(Authenticated { timeout_event_id: 1 }).into();
        let state = state.handle_timeout(&mut r_sta, &mut ctx, 2, ClientEvent::AssociationTimeout);

        let (_, Authenticated { .. }) = match state {
            States::Authenticated(state) => state.release_data(),
            _ => panic!("unexpected state"),
        };

        assert_variant!(mlme_stream.try_next(), Err(_));
    }

    #[test]
    fn authenticated_goes_to_associated_no_rsn() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();

        let state: States =
            State::new(Authenticating).transition_to(Authenticated { timeout_event_id: 1 }).into();

        let mut aid_map = aid::Map::default();
        let state = state.handle_assoc_ind(
            &mut r_sta,
            &mut ctx,
            &mut aid_map,
            CapabilityInfo(0).with_short_preamble(true),
            CapabilityInfo(0).with_short_preamble(true).raw(),
            &[SupportedRate(0b11111000)][..],
            &[0b11111000][..],
            &None,
            None,
        );

        let (_, Associated { rsna_link_state, aid }) = match state {
            States::Associated(state) => state.release_data(),
            _ => panic!("unexpected_state"),
        };

        assert_variant!(rsna_link_state, None);
        assert_eq!(aid, 1);

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::AssocResponse(fidl_mlme::AssociateResponse {
            peer_sta_address,
            result_code,
            cap,
            rates,
            ..
        }) => {
            assert_eq!(peer_sta_address, CLIENT_ADDR);
            assert_eq!(result_code, fidl_mlme::AssociateResultCodes::Success);
            assert_eq!(cap, CapabilityInfo(0).with_short_preamble(true).raw());
            assert_eq!(rates, vec![0b11111000]);
        });
    }

    #[test]
    fn authenticated_goes_to_associated_no_rsn_differing_cap() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();

        let state: States =
            State::new(Authenticating).transition_to(Authenticated { timeout_event_id: 1 }).into();

        let mut aid_map = aid::Map::default();
        let state = state.handle_assoc_ind(
            &mut r_sta,
            &mut ctx,
            &mut aid_map,
            CapabilityInfo(0).with_short_preamble(true).with_spectrum_mgmt(true),
            CapabilityInfo(0)
                .with_short_preamble(true)
                .with_spectrum_mgmt(true)
                .with_radio_measurement(true)
                .raw(),
            &[SupportedRate(0b11111000)][..],
            &[0b11111000][..],
            &None,
            None,
        );

        let (_, Associated { rsna_link_state, aid }) = match state {
            States::Associated(state) => state.release_data(),
            _ => panic!("unexpected_state"),
        };

        assert_variant!(rsna_link_state, None);
        assert_eq!(aid, 1);

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::AssocResponse(fidl_mlme::AssociateResponse {
            peer_sta_address,
            result_code,
            cap,
            rates,
            ..
        }) => {
            assert_eq!(peer_sta_address, CLIENT_ADDR);
            assert_eq!(result_code, fidl_mlme::AssociateResultCodes::Success);
            assert_eq!(
                cap,
                CapabilityInfo(0).with_short_preamble(true).with_spectrum_mgmt(true).raw(),
            );
            assert_eq!(rates, vec![0b11111000]);
        });
    }

    #[test]
    fn authenticated_goes_to_associated_differing_nonbasic_rates() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();

        let state: States =
            State::new(Authenticating).transition_to(Authenticated { timeout_event_id: 1 }).into();

        let mut aid_map = aid::Map::default();
        state.handle_assoc_ind(
            &mut r_sta,
            &mut ctx,
            &mut aid_map,
            CapabilityInfo(0).with_short_preamble(true),
            CapabilityInfo(0).with_short_preamble(true).raw(),
            &[SupportedRate(0b11111000), SupportedRate(0b01111001)][..],
            &[0b11111000, 0b01111010][..],
            &None,
            None,
        );

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::AssocResponse(fidl_mlme::AssociateResponse {
            cap,
            rates,
            ..
        }) => {
            assert_eq!(cap, CapabilityInfo(0).with_short_preamble(true).raw());
            assert_eq!(rates, vec![0b11111000]);
        });
    }

    #[test]
    fn authenticated_goes_to_associated_fullmac() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();

        ctx.device_info.driver_features = vec![];

        let state: States =
            State::new(Authenticating).transition_to(Authenticated { timeout_event_id: 1 }).into();

        let mut aid_map = aid::Map::default();
        state.handle_assoc_ind(
            &mut r_sta,
            &mut ctx,
            &mut aid_map,
            CapabilityInfo(0).with_short_preamble(true),
            CapabilityInfo(0).with_short_preamble(true).raw(),
            &[][..],
            &[0b11111000, 0b01111010][..],
            &None,
            None,
        );

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::AssocResponse(fidl_mlme::AssociateResponse {
            cap,
            rates,
            ..
        }) => {
            assert_eq!(cap, CapabilityInfo(0).with_short_preamble(true).raw());
            assert_eq!(rates, vec![0b11111000, 0b01111010]);
        });
    }

    #[test]
    fn authenticated_goes_to_associated_differing_basic_rates() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();

        let state: States =
            State::new(Authenticating).transition_to(Authenticated { timeout_event_id: 1 }).into();

        let mut aid_map = aid::Map::default();
        state.handle_assoc_ind(
            &mut r_sta,
            &mut ctx,
            &mut aid_map,
            CapabilityInfo(0).with_short_preamble(true),
            CapabilityInfo(0).with_short_preamble(true).raw(),
            &[SupportedRate(0b11111001)][..],
            &[0b11111000][..],
            &None,
            None,
        );

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::AssocResponse(fidl_mlme::AssociateResponse {
            peer_sta_address,
            result_code,
            ..
        }) => {
            assert_eq!(peer_sta_address, CLIENT_ADDR);
            assert_eq!(result_code, fidl_mlme::AssociateResultCodes::RefusedBasicRatesMismatch);
        });

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::Deauthenticate(fidl_mlme::DeauthenticateRequest {
            peer_sta_address,
            reason_code,
            ..
        }) => {
            assert_eq!(peer_sta_address, CLIENT_ADDR);
            assert_eq!(reason_code, fidl_mlme::ReasonCode::ReasonInvalidElement);
        });
    }

    #[test]
    fn authenticated_goes_to_associated_no_ap_rates() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();

        let state: States =
            State::new(Authenticating).transition_to(Authenticated { timeout_event_id: 1 }).into();

        let mut aid_map = aid::Map::default();
        state.handle_assoc_ind(
            &mut r_sta,
            &mut ctx,
            &mut aid_map,
            CapabilityInfo(0).with_short_preamble(true),
            CapabilityInfo(0).with_short_preamble(true).raw(),
            &[SupportedRate(0b01111000)][..],
            &[][..],
            &None,
            None,
        );

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::AssocResponse(fidl_mlme::AssociateResponse {
            peer_sta_address,
            result_code,
            ..
        }) => {
            assert_eq!(peer_sta_address, CLIENT_ADDR);
            assert_eq!(result_code, fidl_mlme::AssociateResultCodes::RefusedCapabilitiesMismatch);
        });

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::Deauthenticate(fidl_mlme::DeauthenticateRequest {
            peer_sta_address,
            reason_code,
            ..
        }) => {
            assert_eq!(peer_sta_address, CLIENT_ADDR);
            assert_eq!(reason_code, fidl_mlme::ReasonCode::ReasonInvalidElement);
        });
    }

    #[test]
    fn authenticated_goes_to_authenticating_out_of_aids() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();

        let state: States =
            State::new(Authenticating).transition_to(Authenticated { timeout_event_id: 1 }).into();

        let mut aid_map = aid::Map::default();
        while let Ok(_) = aid_map.assign_aid() {
            // Keep assigning AIDs until we run out of them.
        }

        let state = state.handle_assoc_ind(
            &mut r_sta,
            &mut ctx,
            &mut aid_map,
            CapabilityInfo(0).with_short_preamble(true),
            CapabilityInfo(0).with_short_preamble(true).raw(),
            &[SupportedRate(0b11111000)][..],
            &[0b11111000][..],
            &None,
            None,
        );

        let (_, Authenticating) = match state {
            States::Authenticating(state) => state.release_data(),
            _ => panic!("unexpected state"),
        };

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::AssocResponse(fidl_mlme::AssociateResponse {
            peer_sta_address,
            result_code,
            ..
        }) => {
            assert_eq!(peer_sta_address, CLIENT_ADDR);
            assert_eq!(result_code, fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified);
        });

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::Deauthenticate(fidl_mlme::DeauthenticateRequest {
            peer_sta_address,
            reason_code,
            ..
        }) => {
            assert_eq!(peer_sta_address, CLIENT_ADDR);
            assert_eq!(reason_code, fidl_mlme::ReasonCode::UnspecifiedReason);
        });
    }

    #[test]
    fn authenticated_goes_to_authenticating_with_bogus_rsn_ind() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();

        let state: States =
            State::new(Authenticating).transition_to(Authenticated { timeout_event_id: 1 }).into();

        let s_rsne = Rsne::wpa2_psk_ccmp_rsne();
        let mut s_rsne_vec = Vec::with_capacity(s_rsne.len());
        s_rsne.write_into(&mut s_rsne_vec).expect("error writing RSNE");

        let mut aid_map = aid::Map::default();
        let state = state.handle_assoc_ind(
            &mut r_sta,
            &mut ctx,
            &mut aid_map,
            CapabilityInfo(0).with_short_preamble(true),
            CapabilityInfo(0).with_short_preamble(true).raw(),
            &[SupportedRate(0b11111000)][..],
            &[0b11111000][..],
            &None,
            Some(s_rsne_vec),
        );

        let (_, Authenticating) = match state {
            States::Authenticating(state) => state.release_data(),
            _ => panic!("unexpected state"),
        };

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::AssocResponse(fidl_mlme::AssociateResponse {
            peer_sta_address,
            result_code,
            ..
        }) => {
            assert_eq!(peer_sta_address, CLIENT_ADDR);
            assert_eq!(result_code, fidl_mlme::AssociateResultCodes::RefusedCapabilitiesMismatch);
        });

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::Deauthenticate(fidl_mlme::DeauthenticateRequest {
            peer_sta_address,
            reason_code,
            ..
        }) => {
            assert_eq!(peer_sta_address, CLIENT_ADDR);
            assert_eq!(reason_code, fidl_mlme::ReasonCode::ReasonInvalidElement);
        });
    }

    #[test]
    fn authenticated_goes_to_authenticating_with_incompatible_rsn() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();

        let state: States =
            State::new(Authenticating).transition_to(Authenticated { timeout_event_id: 1 }).into();

        let mut rsn_cfg = create_rsn_cfg(b"coolnet", b"password").unwrap().unwrap();
        rsn_cfg.rsne = Rsne {
            group_data_cipher_suite: Some(CIPHER_GCMP_256),
            pairwise_cipher_suites: vec![CIPHER_CCMP_128],
            akm_suites: vec![AKM_PSK],
            ..Default::default()
        };

        let s_rsne = Rsne {
            group_data_cipher_suite: Some(CIPHER_CCMP_128),
            pairwise_cipher_suites: vec![CIPHER_CCMP_128],
            akm_suites: vec![AKM_PSK],
            ..Default::default()
        };
        let mut s_rsne_vec = Vec::with_capacity(s_rsne.len());
        s_rsne.write_into(&mut s_rsne_vec).expect("error writing RSNE");

        let mut aid_map = aid::Map::default();
        let state = state.handle_assoc_ind(
            &mut r_sta,
            &mut ctx,
            &mut aid_map,
            CapabilityInfo(0).with_short_preamble(true),
            CapabilityInfo(0).with_short_preamble(true).raw(),
            &[SupportedRate(0b11111000)][..],
            &[0b11111000][..],
            &Some(rsn_cfg),
            Some(s_rsne_vec),
        );

        let (_, Authenticating) = match state {
            States::Authenticating(state) => state.release_data(),
            _ => panic!("unexpected state"),
        };

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::AssocResponse(fidl_mlme::AssociateResponse {
            peer_sta_address,
            result_code,
            ..
        }) => {
            assert_eq!(peer_sta_address, CLIENT_ADDR);
            assert_eq!(result_code, fidl_mlme::AssociateResultCodes::RefusedCapabilitiesMismatch);
        });

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::Deauthenticate(fidl_mlme::DeauthenticateRequest {
            peer_sta_address,
            reason_code,
            ..
        }) => {
            assert_eq!(peer_sta_address, CLIENT_ADDR);
            assert_eq!(reason_code, fidl_mlme::ReasonCode::Ieee8021XAuthFailed);
        });
    }

    #[test]
    fn authenticated_goes_to_associated_rsn() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();

        let state: States =
            State::new(Authenticating).transition_to(Authenticated { timeout_event_id: 1 }).into();

        let rsn_cfg = create_rsn_cfg(b"coolnet", b"password").unwrap().unwrap();

        let mut s_rsne_vec = Vec::with_capacity(rsn_cfg.rsne.len());
        rsn_cfg.rsne.write_into(&mut s_rsne_vec).expect("error writing RSNE");

        let mut aid_map = aid::Map::default();
        let state = state.handle_assoc_ind(
            &mut r_sta,
            &mut ctx,
            &mut aid_map,
            CapabilityInfo(0).with_short_preamble(true),
            CapabilityInfo(0).with_short_preamble(true).raw(),
            &[SupportedRate(0b11111000)][..],
            &[0b11111000][..],
            &Some(rsn_cfg),
            Some(s_rsne_vec),
        );

        let (_, Associated { rsna_link_state, aid }) = match state {
            States::Associated(state) => state.release_data(),
            _ => panic!("unexpected_state"),
        };

        assert_eq!(aid, 1);
        assert_variant!(rsna_link_state, Some(_));

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::AssocResponse(fidl_mlme::AssociateResponse {
            peer_sta_address,
            result_code,
            cap,
            rates,
            ..
        }) => {
            assert_eq!(peer_sta_address, CLIENT_ADDR);
            assert_eq!(result_code, fidl_mlme::AssociateResultCodes::Success);
            assert_eq!(cap, CapabilityInfo(0).with_short_preamble(true).with_privacy(true).raw());
            assert_eq!(rates, vec![0b11111000]);
        });

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::Eapol(fidl_mlme::EapolRequest { .. }));
    }

    #[test]
    fn authenticated_goes_to_associated_rsn_different_cap() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();

        let state: States =
            State::new(Authenticating).transition_to(Authenticated { timeout_event_id: 1 }).into();

        let rsn_cfg = create_rsn_cfg(b"coolnet", b"password").unwrap().unwrap();

        let mut s_rsne_vec = Vec::with_capacity(rsn_cfg.rsne.len());
        rsn_cfg.rsne.write_into(&mut s_rsne_vec).expect("error writing RSNE");

        let mut aid_map = aid::Map::default();
        let state = state.handle_assoc_ind(
            &mut r_sta,
            &mut ctx,
            &mut aid_map,
            CapabilityInfo(0)
                .with_short_preamble(true)
                .with_spectrum_mgmt(true)
                .with_radio_measurement(true),
            CapabilityInfo(0).with_short_preamble(true).with_spectrum_mgmt(true).raw(),
            &[SupportedRate(0b11111000)][..],
            &[0b11111000][..],
            &Some(rsn_cfg),
            Some(s_rsne_vec),
        );

        let (_, Associated { rsna_link_state, aid }) = match state {
            States::Associated(state) => state.release_data(),
            _ => panic!("unexpected_state"),
        };

        assert_eq!(aid, 1);
        assert_variant!(rsna_link_state, Some(_));

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::AssocResponse(fidl_mlme::AssociateResponse {
            peer_sta_address,
            result_code,
            cap,
            rates,
            ..
        }) => {
            assert_eq!(peer_sta_address, CLIENT_ADDR);
            assert_eq!(result_code, fidl_mlme::AssociateResultCodes::Success);
            assert_eq!(
                cap,
                CapabilityInfo(0)
                    .with_short_preamble(true)
                    .with_spectrum_mgmt(true)
                    .with_privacy(true)
                    .raw());
            assert_eq!(rates, vec![0b11111000]);
        });

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::Eapol(fidl_mlme::EapolRequest { .. }));
    }

    #[test]
    fn associated_goes_to_authenticated() {
        let mut r_sta = make_remote_client();
        let (mut ctx, _, mut time_stream) = make_env();
        let mut aid_map = aid::Map::default();

        let aid = aid_map.assign_aid().unwrap();

        let state: States = State::new(Authenticating)
            .transition_to(Authenticated { timeout_event_id: 1 })
            .transition_to(Associated { aid, rsna_link_state: None })
            .into();

        let state = state.handle_disassoc_ind(&mut r_sta, &mut ctx, &mut aid_map);

        let (_, Authenticated { timeout_event_id }) = match state {
            States::Authenticated(state) => state.release_data(),
            _ => panic!("unexpected state"),
        };

        assert_eq!(aid, aid_map.assign_aid().unwrap());

        let (_, timed_event) = time_stream.try_next().unwrap().expect("expected timed event");
        assert_eq!(timed_event.id, timeout_event_id);
        assert_variant!(timed_event.event, Event::Client { addr, event } => {
            assert_eq!(addr, CLIENT_ADDR);
            assert_variant!(event, ClientEvent::AssociationTimeout);
        });
    }

    #[test]
    fn associated_ignores_rsna_negotiation_timeout_without_rsna_link_state() {
        let mut r_sta = make_remote_client();
        let (mut ctx, _, mut time_stream) = make_env();

        let state: States = State::new(Authenticating)
            .transition_to(Authenticated { timeout_event_id: 1 })
            .transition_to(Associated { aid: 1, rsna_link_state: None })
            .into();

        let state = state.handle_timeout(
            &mut r_sta,
            &mut ctx,
            1,
            ClientEvent::RsnaTimeout(RsnaTimeout::Negotiation),
        );

        let (_, Associated { .. }) = match state {
            States::Associated(state) => state.release_data(),
            _ => panic!("unexpected_state"),
        };

        assert_variant!(time_stream.try_next(), Err(_));
    }

    #[test]
    fn associated_ignores_rsna_request_timeout_without_rsna_link_state() {
        let mut r_sta = make_remote_client();
        let (mut ctx, _, mut time_stream) = make_env();

        let state: States = State::new(Authenticating)
            .transition_to(Authenticated { timeout_event_id: 1 })
            .transition_to(Associated { aid: 1, rsna_link_state: None })
            .into();

        let state = state.handle_timeout(
            &mut r_sta,
            &mut ctx,
            1,
            ClientEvent::RsnaTimeout(RsnaTimeout::Request),
        );

        let (_, Associated { .. }) = match state {
            States::Associated(state) => state.release_data(),
            _ => panic!("unexpected_state"),
        };

        assert_variant!(time_stream.try_next(), Err(_));
    }

    #[test]
    fn associated_handles_rsna_request_timeout() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, mut time_stream) = make_env();

        let rsn_cfg = create_rsn_cfg(b"coolnet", b"password").unwrap().unwrap();

        let s_rsne = Rsne::wpa2_psk_ccmp_rsne();
        let mut s_rsne_vec = Vec::with_capacity(s_rsne.len());
        s_rsne.write_into(&mut s_rsne_vec).expect("error writing RSNE");

        let state: States = State::new(Authenticating)
            .transition_to(Authenticated { timeout_event_id: 1 })
            .transition_to(Associated {
                aid: 1,
                rsna_link_state: Some(RsnaLinkState {
                    request_attempts: 0,
                    last_key_frame: Some(test_utils::eapol_key_frame()),
                    request_timeout_event_id: Some(1),
                    negotiation_timeout_event_id: Some(2),
                    authenticator: new_authenticator_from_rsne(
                        AP_ADDR,
                        CLIENT_ADDR,
                        &s_rsne_vec[..],
                        &rsn_cfg,
                    )
                    .unwrap(),
                }),
            })
            .into();

        let state = state.handle_timeout(
            &mut r_sta,
            &mut ctx,
            1,
            ClientEvent::RsnaTimeout(RsnaTimeout::Request),
        );

        let (_, Associated { rsna_link_state, .. }) = match state {
            States::Associated(state) => state.release_data(),
            _ => panic!("unexpected_state"),
        };

        assert_eq!(rsna_link_state.as_ref().unwrap().request_attempts, 1);

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::Eapol(fidl_mlme::EapolRequest { .. }));

        let (_, timed_event) = time_stream.try_next().unwrap().expect("expected timed event");
        assert_eq!(
            timed_event.id,
            rsna_link_state.as_ref().unwrap().request_timeout_event_id.unwrap()
        );
        assert_variant!(timed_event.event, Event::Client { addr, event } => {
            assert_eq!(addr, CLIENT_ADDR);
            assert_variant!(event, ClientEvent::RsnaTimeout(RsnaTimeout::Request));
        });
    }

    #[test]
    fn associated_handles_rsna_negotiation_timeout() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();

        let rsn_cfg = create_rsn_cfg(b"coolnet", b"password").unwrap().unwrap();

        let s_rsne = Rsne::wpa2_psk_ccmp_rsne();
        let mut s_rsne_vec = Vec::with_capacity(s_rsne.len());
        s_rsne.write_into(&mut s_rsne_vec).expect("error writing RSNE");

        let state: States = State::new(Authenticating)
            .transition_to(Authenticated { timeout_event_id: 1 })
            .transition_to(Associated {
                aid: 1,
                rsna_link_state: Some(RsnaLinkState {
                    request_attempts: 3,
                    last_key_frame: Some(test_utils::eapol_key_frame()),
                    request_timeout_event_id: Some(1),
                    negotiation_timeout_event_id: Some(2),
                    authenticator: new_authenticator_from_rsne(
                        AP_ADDR,
                        CLIENT_ADDR,
                        &s_rsne_vec[..],
                        &rsn_cfg,
                    )
                    .unwrap(),
                }),
            })
            .into();

        let state = state.handle_timeout(
            &mut r_sta,
            &mut ctx,
            2,
            ClientEvent::RsnaTimeout(RsnaTimeout::Negotiation),
        );

        let (_, Authenticating) = match state {
            States::Authenticating(state) => state.release_data(),
            _ => panic!("unexpected_state"),
        };

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::Deauthenticate(fidl_mlme::DeauthenticateRequest {
            peer_sta_address,
            reason_code,
        }) => {
            assert_eq!(peer_sta_address, CLIENT_ADDR);
            assert_eq!(reason_code, fidl_mlme::ReasonCode::FourwayHandshakeTimeout);
        });
    }

    #[test]
    fn associated_handles_rsna_key_frame_resets_request_attempts() {
        let mut r_sta = make_remote_client();
        let (mut ctx, _, _) = make_env();

        let rsn_cfg = create_rsn_cfg(b"coolnet", b"password").unwrap().unwrap();

        let s_rsne = Rsne::wpa2_psk_ccmp_rsne();
        let mut s_rsne_vec = Vec::with_capacity(s_rsne.len());
        s_rsne.write_into(&mut s_rsne_vec).expect("error writing RSNE");

        let state: States = State::new(Authenticating)
            .transition_to(Authenticated { timeout_event_id: 1 })
            .transition_to(Associated {
                aid: 1,
                rsna_link_state: Some(RsnaLinkState {
                    request_attempts: 3,
                    last_key_frame: Some(test_utils::eapol_key_frame()),
                    request_timeout_event_id: Some(1),
                    negotiation_timeout_event_id: Some(2),
                    authenticator: new_authenticator_from_rsne(
                        AP_ADDR,
                        CLIENT_ADDR,
                        &s_rsne_vec[..],
                        &rsn_cfg,
                    )
                    .unwrap(),
                }),
            })
            .into();

        let state = state.handle_eapol_ind(
            &mut r_sta,
            &mut ctx,
            &Vec::<u8>::from(test_utils::eapol_key_frame())[..],
        );

        let (_, Associated { rsna_link_state, .. }) = match state {
            States::Associated(state) => state.release_data(),
            _ => panic!("unexpected_state"),
        };

        assert_eq!(rsna_link_state.as_ref().unwrap().request_attempts, 0);
    }

    #[test]
    fn associated_handles_rsna_request_timeout_last_attempt() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();

        let rsn_cfg = create_rsn_cfg(b"coolnet", b"password").unwrap().unwrap();

        let s_rsne = Rsne::wpa2_psk_ccmp_rsne();
        let mut s_rsne_vec = Vec::with_capacity(s_rsne.len());
        s_rsne.write_into(&mut s_rsne_vec).expect("error writing RSNE");

        let state: States = State::new(Authenticating)
            .transition_to(Authenticated { timeout_event_id: 1 })
            .transition_to(Associated {
                aid: 1,
                rsna_link_state: Some(RsnaLinkState {
                    request_attempts: 3,
                    last_key_frame: Some(test_utils::eapol_key_frame()),
                    request_timeout_event_id: Some(1),
                    negotiation_timeout_event_id: Some(2),
                    authenticator: new_authenticator_from_rsne(
                        AP_ADDR,
                        CLIENT_ADDR,
                        &s_rsne_vec[..],
                        &rsn_cfg,
                    )
                    .unwrap(),
                }),
            })
            .into();

        let state = state.handle_timeout(
            &mut r_sta,
            &mut ctx,
            1,
            ClientEvent::RsnaTimeout(RsnaTimeout::Request),
        );

        let (_, Authenticating) = match state {
            States::Authenticating(state) => state.release_data(),
            _ => panic!("unexpected state"),
        };

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::Deauthenticate(fidl_mlme::DeauthenticateRequest {
            peer_sta_address,
            reason_code,
        }) => {
            assert_eq!(peer_sta_address, CLIENT_ADDR);
            assert_eq!(reason_code, fidl_mlme::ReasonCode::FourwayHandshakeTimeout);
        });
    }

    #[test]
    fn associated_handles_eapol_key_frame() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();

        let state: States = State::new(Authenticating)
            .transition_to(Authenticated { timeout_event_id: 1 })
            .transition_to(Associated {
                aid: 1,
                rsna_link_state: Some(RsnaLinkState {
                    request_attempts: 0,
                    last_key_frame: Some(test_utils::eapol_key_frame()),
                    request_timeout_event_id: Some(1),
                    negotiation_timeout_event_id: Some(2),
                    authenticator: Box::new(MockAuthenticator::new(
                        Arc::new(Mutex::new(vec![])),
                        Arc::new(Mutex::new(vec![SecAssocUpdate::TxEapolKeyFrame(
                            test_utils::eapol_key_frame(),
                        )])),
                    )),
                }),
            })
            .into();

        state.handle_eapol_ind(
            &mut r_sta,
            &mut ctx,
            &Vec::<u8>::from(test_utils::eapol_key_frame())[..],
        );

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::Eapol(fidl_mlme::EapolRequest {
            src_addr,
            dst_addr,
            data,
        }) => {
            assert_eq!(src_addr, AP_ADDR);
            assert_eq!(dst_addr, CLIENT_ADDR);
            assert_eq!(data, Vec::<u8>::from(test_utils::eapol_key_frame()));
        });
    }

    #[test]
    fn associated_handles_eapol_key() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();

        let state: States = State::new(Authenticating)
            .transition_to(Authenticated { timeout_event_id: 1 })
            .transition_to(Associated {
                aid: 1,
                rsna_link_state: Some(RsnaLinkState {
                    request_attempts: 0,
                    last_key_frame: Some(test_utils::eapol_key_frame()),
                    request_timeout_event_id: Some(1),
                    negotiation_timeout_event_id: Some(2),
                    authenticator: Box::new(MockAuthenticator::new(
                        Arc::new(Mutex::new(vec![])),
                        Arc::new(Mutex::new(vec![SecAssocUpdate::Key(
                            Key::Ptk(test_utils::ptk()),
                        )])),
                    )),
                }),
            })
            .into();

        state.handle_eapol_ind(
            &mut r_sta,
            &mut ctx,
            &Vec::<u8>::from(test_utils::eapol_key_frame())[..],
        );

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::SetKeys(fidl_mlme::SetKeysRequest { keylist }) => {
            assert_eq!(keylist.len(), 1);
            let k = keylist.get(0).expect("expect key descriptor");
            assert_eq!(k.key, vec![0xCCu8; test_utils::cipher().tk_bytes().unwrap()]);
            assert_eq!(k.key_id, 0);
            assert_eq!(k.key_type, fidl_mlme::KeyType::Pairwise);
            assert_eq!(k.address, CLIENT_ADDR);
            assert_eq!(k.rsc, 0);
            assert_eq!(k.cipher_suite_oui, [0x00, 0x0F, 0xAC]);
            assert_eq!(k.cipher_suite_type, 4);
        });
    }

    #[test]
    fn associated_handles_esssa_established() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();

        let state: States = State::new(Authenticating)
            .transition_to(Authenticated { timeout_event_id: 1 })
            .transition_to(Associated {
                aid: 1,
                rsna_link_state: Some(RsnaLinkState {
                    request_attempts: 0,
                    last_key_frame: Some(test_utils::eapol_key_frame()),
                    request_timeout_event_id: Some(1),
                    negotiation_timeout_event_id: Some(2),
                    authenticator: Box::new(MockAuthenticator::new(
                        Arc::new(Mutex::new(vec![])),
                        Arc::new(Mutex::new(vec![SecAssocUpdate::Status(
                            SecAssocStatus::EssSaEstablished,
                        )])),
                    )),
                }),
            })
            .into();

        let state = state.handle_eapol_ind(
            &mut r_sta,
            &mut ctx,
            &Vec::<u8>::from(test_utils::eapol_key_frame())[..],
        );

        let (_, Associated { rsna_link_state, .. }) = match state {
            States::Associated(state) => state.release_data(),
            _ => panic!("unexpected_state"),
        };

        assert_variant!(&rsna_link_state.as_ref().unwrap().last_key_frame, None);
        assert_variant!(&rsna_link_state.as_ref().unwrap().request_timeout_event_id, None);
        assert_variant!(&rsna_link_state.as_ref().unwrap().negotiation_timeout_event_id, None);

        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::SetCtrlPort(fidl_mlme::SetControlledPortRequest {
            peer_sta_address,
            state,
        }) => {
            assert_eq!(peer_sta_address, CLIENT_ADDR);
            assert_eq!(state, fidl_mlme::ControlledPortState::Open);
        });
    }
}
