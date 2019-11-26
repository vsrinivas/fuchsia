// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A state machine for associating a Client to a BSS.
//! Note: This implementation only supports simultaneous authentication with exactly one STA, the
//! AP. While 802.11 explicitly allows - and sometime requires - authentication with more than one
//! STA, Fuchsia does intentionally not yet support this use-case.

use {
    crate::{
        auth,
        client::{Client, TimedEvent},
        timer::*,
    },
    fidl_fuchsia_wlan_mlme as fidl_mlme,
    log::{error, info, warn},
    wlan_common::{mac, time::TimeUnit},
    wlan_statemachine::*,
    zerocopy::ByteSlice,
};

/// Association timeout in Beacon periods.
/// If no association response was received from he BSS within this time window, an association is
/// considered to have failed.
// TODO(41609): Let upper layers set this value.
const ASSOC_TIMEOUT_BCN_PERIODS: u16 = 10;
/// AID used for reporting failed associations to SME.
const FAILED_ASSOCIATION_AID: mac::Aid = 0;

/// Processes an inbound deauthentication frame by issuing an MLME-DEAUTHENTICATE.indication
/// to the STA's SME peer.
trait DeauthenticationHandler {
    /// Sends an MLME-DEAUTHENTICATE.indication message to MLME's SME peer.
    fn on_deauth_frame(&self, sta: &mut Client, deauth_hdr: &mac::DeauthHdr) {
        let reason_code = fidl_mlme::ReasonCode::from_primitive(deauth_hdr.reason_code.0)
            .unwrap_or(fidl_mlme::ReasonCode::UnspecifiedReason);
        sta.send_deauthenticate_ind(reason_code);
    }
}

/// Client joined a BSS (synchronized timers and prepared its underlying hardware).
/// At this point the Client is able to listen to frames on the BSS' channel.
pub struct Joined;

impl Joined {
    /// Initiates an open authentication with the currently joined BSS.
    /// The returned state is unchanged in an error case. Otherwise, the state transitions into
    /// "Authenticating".
    /// Returns Ok(timeout) if authentication request was sent successfully, Err(()) otherwise.
    fn authenticate(&self, sta: &mut Client, timeout_bcn_count: u8) -> Result<EventId, ()> {
        match sta.send_open_auth_frame() {
            Ok(()) => {
                let duration_tus = TimeUnit::DEFAULT_BEACON_INTERVAL * timeout_bcn_count;
                let deadline = sta.timer.now() + duration_tus.into();
                let event = TimedEvent::Authenticating;
                let event_id = sta.timer.schedule_event(deadline, event);
                Ok(event_id)
            }
            Err(e) => {
                error!("{}", e);
                sta.send_authenticate_conf(fidl_mlme::AuthenticateResultCodes::Refused);
                Err(())
            }
        }
    }
}

/// Client issued an authentication request frame to its joined BSS prior to joining this state.
/// At this point the client is waiting for an authentication response frame from the client.
/// Note: This assumes Open System authentication.
pub struct Authenticating {
    timeout: EventId,
}

impl Authenticating {
    /// Processes an inbound authentication frame.
    /// SME will be notified via an MLME-AUTHENTICATE.confirm message whether the authentication
    /// with the BSS was successful.
    /// Returns Ok(()) if the authentication was successful, otherwise Err(()).
    /// Note: The pending authentication timeout will be canceled in any case.
    fn on_auth_frame(&self, sta: &mut Client, auth_hdr: &mac::AuthHdr) -> Result<(), ()> {
        sta.timer.cancel_event(self.timeout);

        match auth::is_valid_open_ap_resp(auth_hdr) {
            Ok(()) => {
                sta.send_authenticate_conf(fidl_mlme::AuthenticateResultCodes::Success);
                Ok(())
            }
            Err(e) => {
                error!("authentication with BSS failed: {}", e);
                sta.send_authenticate_conf(fidl_mlme::AuthenticateResultCodes::Refused);
                Err(())
            }
        }
    }

    /// Invoked when the pending timeout fired. The original authentication request is now
    /// considered to be expired and invalid - the authentication failed. As a consequence,
    /// an MLME-AUTHENTICATION.confirm message is reported to MLME's SME peer indicating the
    /// timeout.
    fn on_timeout(&self, sta: &mut Client) {
        // At this point, the event should already be canceled by the state's owner. However,
        // ensure the timeout is canceled in any case.
        sta.timer.cancel_event(self.timeout);

        sta.send_authenticate_conf(fidl_mlme::AuthenticateResultCodes::AuthFailureTimeout);
    }
}

impl DeauthenticationHandler for Authenticating {
    /// Processes an inbound deauthentication frame.
    /// This always results in an MLME-AUTHENTICATE.confirm message to MLME's SME peer.
    /// The pending authentication timeout will be canceled in this process.
    fn on_deauth_frame(&self, sta: &mut Client, deauth_hdr: &mac::DeauthHdr) {
        sta.timer.cancel_event(self.timeout);

        info!(
            "received spurious deauthentication frame while authenticating with BSS (unusual); \
             authentication failed: {:?}",
            { deauth_hdr.reason_code }
        );
        sta.send_authenticate_conf(fidl_mlme::AuthenticateResultCodes::Refused);
    }
}

/// Client received a "successful" authentication response from the BSS.
pub struct Authenticated;
impl DeauthenticationHandler for Authenticated {}

impl Authenticated {
    /// Initiates an association with the currently joined BSS.
    /// Returns Ok(timeout) if association request was sent successfully.
    /// Otherwise an Err(()) is returned and an ASSOCIATE.confirm message to its SME peer.
    fn associate(&self, sta: &mut Client) -> Result<EventId, ()> {
        // TODO(eyw) Send actual IEs.
        let ssid = vec![];
        let rates = vec![1, 2, 8];
        let rsne = vec![];
        let ht_cap = vec![];
        let vht_cap = vec![];
        match sta.send_assoc_req_frame(
            0,
            &ssid[..],
            &rates[..],
            &rsne[..],
            &ht_cap[..],
            &vht_cap[..],
        ) {
            Ok(()) => {
                let duration_tus = TimeUnit::DEFAULT_BEACON_INTERVAL * ASSOC_TIMEOUT_BCN_PERIODS;
                let deadline = sta.timer.now() + duration_tus.into();
                let event = TimedEvent::Associating;
                let event_id = sta.timer.schedule_event(deadline, event);
                Ok(event_id)
            }
            Err(e) => {
                error!("{}", e);
                sta.send_associate_conf(
                    FAILED_ASSOCIATION_AID,
                    fidl_mlme::AssociateResultCodes::RefusedTemporarily,
                );
                Err(())
            }
        }
    }
}

/// Client received an MLME-ASSOCIATE.request message from SME.
pub struct Associating {
    timeout: EventId,
}

impl DeauthenticationHandler for Associating {
    /// Processes an inbound deauthentication frame.
    /// This always results in an MLME-ASSOCIATE.confirm message to MLME's SME peer.
    /// The pending association timeout will be canceled in this process.
    fn on_deauth_frame(&self, sta: &mut Client, deauth_hdr: &mac::DeauthHdr) {
        sta.timer.cancel_event(self.timeout);

        info!(
            "received spurious deauthentication frame while associating with BSS (unusual); \
             association failed: {:?}",
            { deauth_hdr.reason_code }
        );
        sta.send_associate_conf(
            FAILED_ASSOCIATION_AID,
            fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified,
        );
    }
}

impl Associating {
    /// Processes an inbound association response frame.
    /// SME will be notified via an MLME-ASSOCIATE.confirm message whether the association
    /// with the BSS was successful.
    /// Returns Ok(()) if the association was successful, otherwise Err(()).
    /// Note: The pending authentication timeout will be canceled in any case.
    fn on_assoc_resp_frame(
        &self,
        sta: &mut Client,
        assoc_resp_hdr: &mac::AssocRespHdr,
    ) -> Result<ControlledPort, ()> {
        sta.timer.cancel_event(self.timeout);

        match assoc_resp_hdr.status_code {
            mac::StatusCode::SUCCESS => {
                sta.send_associate_conf(
                    assoc_resp_hdr.aid,
                    fidl_mlme::AssociateResultCodes::Success,
                );
                Ok(if sta.is_rsn { ControlledPort::Closed } else { ControlledPort::Open })
            }
            status_code => {
                error!("association with BSS failed: {:?}", status_code);
                sta.send_associate_conf(
                    FAILED_ASSOCIATION_AID,
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
    fn on_disassoc_frame(&self, sta: &mut Client, _disassoc_hdr: &mac::DisassocHdr) {
        sta.timer.cancel_event(self.timeout);

        warn!("received unexpected disassociation frame while associating");
        sta.send_associate_conf(
            FAILED_ASSOCIATION_AID,
            fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified,
        );
    }

    /// Invoked when the pending timeout fired. The original association request is now
    /// considered to be expired and invalid - the association failed. As a consequence,
    /// an MLME-ASSOCIATE.confirm message is reported to MLME's SME peer indicating the
    /// timeout.
    fn on_timeout(&self, sta: &mut Client) {
        // At this point, the event should already be canceled by the state's owner. However,
        // ensure the timeout is canceled in any case.
        sta.timer.cancel_event(self.timeout);

        sta.send_associate_conf(
            FAILED_ASSOCIATION_AID,
            fidl_mlme::AssociateResultCodes::RefusedTemporarily,
        );
    }
}

/// Represents an 802.1X controlled port.
/// A closed controlled port only processes EAP frames while an open one processes any frames.
#[derive(Debug, PartialEq)]
enum ControlledPort {
    Open,
    Closed,
}

impl ControlledPort {
    fn is_opened(&self) -> bool {
        match self {
            ControlledPort::Open => true,
            _ => false,
        }
    }
}

/// Client received a "successful" association response from the BSS.
pub struct Associated {
    controlled_port: ControlledPort,
}
impl DeauthenticationHandler for Associated {}

impl Associated {
    /// Processes inbound data frames.
    fn on_data_frame<B: ByteSlice>(
        &self,
        sta: &mut Client,
        fixed_data_fields: &mac::FixedDataHdrFields,
        addr4: Option<mac::Addr4>,
        qos_ctrl: Option<mac::QosControl>,
        body: B,
    ) {
        let is_controlled_port_open = self.controlled_port.is_opened();
        sta.handle_data_frame(fixed_data_fields, addr4, qos_ctrl, body, is_controlled_port_open);
    }

    /// Processes an inbound diassociation frame.
    /// This always results in an MLME-DISASSOCIATE.indication message to MLME's SME peer.
    fn on_disassoc_frame(&self, sta: &mut Client, disassoc_hdr: &mac::DisassocHdr) {
        let reason_code = fidl_mlme::ReasonCode::from_primitive(disassoc_hdr.reason_code.0)
            .unwrap_or(fidl_mlme::ReasonCode::UnspecifiedReason);
        sta.send_disassoc_ind(reason_code);
    }
}

statemachine!(
    /// Client state machine.
    /// Note: Only authentication is supported right now.
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

    // TODO(hahnr): Handle lost BSS.
);

impl States {
    /// Returns the STA's initial state.
    pub fn new_initial() -> States {
        States::from(State::new(Joined))
    }

    /// Only Open System authentication is supported.
    /// Shared Key authentication is intentionally unsupported within Fuchsia.
    /// SAE will be supported sometime in the future.
    pub fn authenticate(self, sta: &mut Client, timeout_bcn_count: u8) -> States {
        match self {
            // MLME-AUTHENTICATE.request messages are only processed when the Client is "Joined".
            States::Joined(state) => match state.authenticate(sta, timeout_bcn_count) {
                Ok(timeout) => state.transition_to(Authenticating { timeout }).into(),
                Err(()) => state.into(),
            },
            // Reject MLME-AUTHENTICATE.request if STA is not in "Joined" state.
            _ => {
                error!("received MLME-AUTHENTICATE.request in invalid state");
                sta.send_authenticate_conf(fidl_mlme::AuthenticateResultCodes::Refused);
                self
            }
        }
    }

    /// Initates an association with the joined BSS.
    // TODO(eyw): Carry MLME-ASSOCIATE.request information to construct correct association frame.
    pub fn associate(self, sta: &mut Client) -> States {
        match self {
            States::Authenticated(state) => match state.associate(sta) {
                Ok(timeout) => state.transition_to(Associating { timeout }).into(),
                Err(()) => state.into(),
            },
            _ => {
                error!("received MLME-ASSOCIATE.request in invalid state");
                sta.send_associate_conf(
                    FAILED_ASSOCIATION_AID,
                    fidl_mlme::AssociateResultCodes::RefusedNotAuthenticated,
                );
                self
            }
        }
    }

    // TODO(hahnr): Add functionality to open/close controlled port.

    /// Callback to process arbitrary IEEE 802.11 frames.
    /// Frames are dropped if:
    /// - frames are corrupted (too short)
    /// - frames' frame class is not yet permitted
    pub fn on_mac_frame<B: ByteSlice>(
        self,
        sta: &mut Client,
        bytes: B,
        body_aligned: bool,
    ) -> States {
        // Parse mac frame. Drop corrupted ones.
        let mac_frame = match mac::MacFrame::parse(bytes, body_aligned) {
            Some(mac_frame) => mac_frame,
            None => return self,
        };

        // Drop frames which are not permitted in the STA's current state.
        let frame_class = mac::FrameClass::from(&mac_frame);
        if !self.is_frame_class_permitted(frame_class) {
            return self;
        }

        match mac_frame {
            mac::MacFrame::Mgmt { mgmt_hdr, body, .. } => self.on_mgmt_frame(sta, &mgmt_hdr, body),
            mac::MacFrame::Data { fixed_fields, addr4, qos_ctrl, body, .. } => {
                if let States::Associated(state) = &self {
                    state.on_data_frame(
                        sta,
                        &fixed_fields,
                        addr4.map(|x| *x),
                        qos_ctrl.map(|x| x.get()),
                        body,
                    );
                }

                // Drop data frames in all other states
                self
            }
            // Data and Control frames are not yet supported. Drop them.
            _ => self,
        }
    }

    /// Processes inbound management frames.
    /// Only frames from the joined BSS are processed. Frames from other STAs are dropped.
    fn on_mgmt_frame<B: ByteSlice>(
        self,
        sta: &mut Client,
        mgmt_hdr: &mac::MgmtHdr,
        body: B,
    ) -> States {
        if mgmt_hdr.addr3 != sta.bssid.0 {
            return self;
        }

        // Parse management frame. Drop corrupted ones.
        let mgmt_body = match mac::MgmtBody::parse({ mgmt_hdr.frame_ctrl }.mgmt_subtype(), body) {
            Some(x) => x,
            None => return self,
        };

        match self {
            States::Authenticating(state) => match mgmt_body {
                mac::MgmtBody::Authentication { auth_hdr, .. } => {
                    match state.on_auth_frame(sta, &auth_hdr) {
                        Ok(()) => state.transition_to(Authenticated).into(),
                        Err(()) => state.transition_to(Joined).into(),
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
                mac::MgmtBody::AssociationResp { assoc_resp_hdr, .. } => {
                    match state.on_assoc_resp_frame(sta, &assoc_resp_hdr) {
                        Ok(controlled_port) => {
                            state.transition_to(Associated { controlled_port }).into()
                        }
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
            States::Associated(state) => match mgmt_body {
                mac::MgmtBody::Deauthentication { deauth_hdr, .. } => {
                    state.on_deauth_frame(sta, &deauth_hdr);
                    state.transition_to(Joined).into()
                }
                mac::MgmtBody::Disassociation { disassoc_hdr, .. } => {
                    state.on_disassoc_frame(sta, &disassoc_hdr);
                    state.transition_to(Authenticated).into()
                }
                _ => state.into(),
            },
            _ => self,
        }
    }

    /// Callback when a previously scheduled event fired.
    pub fn on_timed_event(self, sta: &mut Client, event_id: EventId) -> States {
        // Lookup the event matching the given id.
        let event = match sta.timer.triggered(&event_id) {
            Some(event) => event,
            None => {
                error!(
                    "event for given ID already consumed;\
                     this should NOT happen - ignoring event"
                );
                return self;
            }
        };

        // Process event.
        match event {
            TimedEvent::Authenticating => match self {
                States::Authenticating(state) => {
                    state.on_timeout(sta);
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
            _ => self,
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
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            buffer::FakeBufferProvider,
            device::{Device, FakeDevice},
        },
        fuchsia_zircon::{self as zx, DurationNum},
        wlan_common::{
            assert_variant,
            mac::{Bssid, MacAddr},
            test_utils::fake_frames::*,
        },
        wlan_statemachine as statemachine,
    };

    const BSSID: Bssid = Bssid([6u8; 6]);
    const IFACE_MAC: MacAddr = [7u8; 6];

    fn make_client_station(device: Device, scheduler: Scheduler) -> Client {
        let buf_provider = FakeBufferProvider::new();
        let client = Client::new(device, buf_provider, scheduler, BSSID, IFACE_MAC, false);
        client
    }

    fn make_protected_client_station(device: Device, scheduler: Scheduler) -> Client {
        let buf_provider = FakeBufferProvider::new();
        let client = Client::new(device, buf_provider, scheduler, BSSID, IFACE_MAC, true);
        client
    }

    #[test]
    fn join_state_authenticate_success() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let state = Joined;
        let timeout_id = state.authenticate(&mut sta, 10).expect("failed authenticating");

        // Verify an event was queued up in the timer.
        assert_variant!(sta.timer.triggered(&timeout_id), Some(TimedEvent::Authenticating));

        // Verify authentication frame was sent to AP.
        assert_eq!(device.wlan_queue.len(), 1);
        let (frame, _txflags) = device.wlan_queue.remove(0);
        #[rustfmt::skip]
        let expected = vec![
            // Mgmt Header:
            0b1011_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // Addr1
            7, 7, 7, 7, 7, 7, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Auth Header:
            0, 0, // Algorithm Number (Open)
            1, 0, // Txn Sequence Number
            0, 0, // Status Code
        ];
        assert_eq!(&frame[..], &expected[..]);

        // Verify no MLME message was sent yet.
        device.next_mlme_msg::<fidl_mlme::AuthenticateConfirm>().expect_err("unexpected message");
    }

    #[test]
    fn join_state_authenticate_tx_failure() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta =
            make_client_station(device.as_device_fail_wlan_tx(), scheduler.as_scheduler());

        let state = Joined;
        state.authenticate(&mut sta, 10).expect_err("should fail authenticating");

        // Verify no event was queued up in the timer.
        assert_eq!(sta.timer.scheduled_event_count(), 0);

        // Verify MLME-AUTHENTICATE.confirm message was sent.
        let msg = device.next_mlme_msg::<fidl_mlme::AuthenticateConfirm>().expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::AuthenticateConfirm {
                peer_sta_address: BSSID.0,
                auth_type: fidl_mlme::AuthenticationTypes::OpenSystem,
                result_code: fidl_mlme::AuthenticateResultCodes::Refused,
                auth_content: None,
            }
        );
    }

    #[test]
    fn authenticating_state_auth_success() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let timeout =
            sta.timer.schedule_event(zx::Time::after(1.seconds()), TimedEvent::Authenticating);
        let state = Authenticating { timeout };

        // Verify authentication was considered successful.
        state
            .on_auth_frame(
                &mut sta,
                &mac::AuthHdr {
                    auth_alg_num: mac::AuthAlgorithmNumber::OPEN,
                    auth_txn_seq_num: 2,
                    status_code: mac::StatusCode::SUCCESS,
                },
            )
            .expect("failed processing auth frame");

        // Verify timeout was canceled.
        assert_variant!(sta.timer.triggered(&timeout), None);

        // Verify MLME-AUTHENTICATE.confirm message was sent.
        let msg = device.next_mlme_msg::<fidl_mlme::AuthenticateConfirm>().expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::AuthenticateConfirm {
                peer_sta_address: BSSID.0,
                auth_type: fidl_mlme::AuthenticationTypes::OpenSystem,
                result_code: fidl_mlme::AuthenticateResultCodes::Success,
                auth_content: None,
            }
        );
    }

    #[test]
    fn authenticating_state_auth_rejected() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let timeout =
            sta.timer.schedule_event(zx::Time::after(1.seconds()), TimedEvent::Authenticating);
        let state = Authenticating { timeout };

        // Verify authentication was considered successful.
        state
            .on_auth_frame(
                &mut sta,
                &mac::AuthHdr {
                    auth_alg_num: mac::AuthAlgorithmNumber::OPEN,
                    auth_txn_seq_num: 2,
                    status_code: mac::StatusCode::NOT_IN_SAME_BSS,
                },
            )
            .expect_err("expected failure processing auth frame");

        // Verify timeout was canceled.
        assert_variant!(sta.timer.triggered(&timeout), None);

        // Verify MLME-AUTHENTICATE.confirm message was sent.
        let msg = device.next_mlme_msg::<fidl_mlme::AuthenticateConfirm>().expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::AuthenticateConfirm {
                peer_sta_address: BSSID.0,
                auth_type: fidl_mlme::AuthenticationTypes::OpenSystem,
                result_code: fidl_mlme::AuthenticateResultCodes::Refused,
                auth_content: None,
            }
        );
    }

    #[test]
    fn authenticating_state_timeout() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let timeout =
            sta.timer.schedule_event(zx::Time::after(1.seconds()), TimedEvent::Authenticating);
        let state = Authenticating { timeout };

        state.on_timeout(&mut sta);

        // Verify timeout was canceled.
        assert_variant!(sta.timer.triggered(&timeout), None);

        // Verify MLME-AUTHENTICATE.confirm message was sent.
        let msg = device.next_mlme_msg::<fidl_mlme::AuthenticateConfirm>().expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::AuthenticateConfirm {
                peer_sta_address: BSSID.0,
                auth_type: fidl_mlme::AuthenticationTypes::OpenSystem,
                result_code: fidl_mlme::AuthenticateResultCodes::AuthFailureTimeout,
                auth_content: None,
            }
        );
    }

    #[test]
    fn authenticating_state_deauth() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let timeout =
            sta.timer.schedule_event(zx::Time::after(1.seconds()), TimedEvent::Authenticating);
        let state = Authenticating { timeout };

        state.on_deauth_frame(
            &mut sta,
            &mac::DeauthHdr { reason_code: mac::ReasonCode::NO_MORE_STAS },
        );

        // Verify timeout was canceled.
        assert_variant!(sta.timer.triggered(&timeout), None);

        // Verify MLME-AUTHENTICATE.confirm message was sent.
        let msg = device.next_mlme_msg::<fidl_mlme::AuthenticateConfirm>().expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::AuthenticateConfirm {
                peer_sta_address: BSSID.0,
                auth_type: fidl_mlme::AuthenticationTypes::OpenSystem,
                result_code: fidl_mlme::AuthenticateResultCodes::Refused,
                auth_content: None,
            }
        );
    }

    #[test]
    fn authenticated_state_deauth() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let state = Authenticated;

        state.on_deauth_frame(
            &mut sta,
            &mac::DeauthHdr { reason_code: mac::ReasonCode::NO_MORE_STAS },
        );

        // Verify MLME-DEAUTHENTICATE.indication message was sent.
        let msg =
            device.next_mlme_msg::<fidl_mlme::DeauthenticateIndication>().expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::DeauthenticateIndication {
                peer_sta_address: BSSID.0,
                reason_code: fidl_mlme::ReasonCode::NoMoreStas,
            }
        );
    }

    #[test]
    fn associating_success_unprotected() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let timeout =
            sta.timer.schedule_event(sta.timer.now() + 1.seconds(), TimedEvent::Associating);
        let state = Associating { timeout };

        // Verify authentication was considered successful.
        let controlled_port = state
            .on_assoc_resp_frame(
                &mut sta,
                &mac::AssocRespHdr {
                    aid: 42,
                    capabilities: mac::CapabilityInfo(52),
                    status_code: mac::StatusCode::SUCCESS,
                },
            )
            .expect("failed processing association response frame");
        assert_eq!(ControlledPort::Open, controlled_port);

        // Verify timeout was canceled.
        assert_variant!(sta.timer.triggered(&timeout), None);

        // Verify MLME-ASSOCIATE.confirm message was sent.
        let msg = device.next_mlme_msg::<fidl_mlme::AssociateConfirm>().expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::AssociateConfirm {
                association_id: 42,
                result_code: fidl_mlme::AssociateResultCodes::Success,
            }
        );
    }

    #[test]
    fn associating_success_protected() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_protected_client_station(device.as_device(), scheduler.as_scheduler());
        let timeout =
            sta.timer.schedule_event(zx::Time::after(1.seconds()), TimedEvent::Associating);
        let state = Associating { timeout };

        // Verify authentication was considered successful.
        let controlled_port = state
            .on_assoc_resp_frame(
                &mut sta,
                &mac::AssocRespHdr {
                    aid: 42,
                    capabilities: mac::CapabilityInfo(52),
                    status_code: mac::StatusCode::SUCCESS,
                },
            )
            .expect("failed processing association response frame");
        assert_eq!(ControlledPort::Closed, controlled_port);

        // Verify timeout was canceled.
        assert_variant!(sta.timer.triggered(&timeout), None);

        // Verify MLME-ASSOCIATE.confirm message was sent.
        let msg = device.next_mlme_msg::<fidl_mlme::AssociateConfirm>().expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::AssociateConfirm {
                association_id: 42,
                result_code: fidl_mlme::AssociateResultCodes::Success,
            }
        );
    }

    #[test]
    fn associating_failure() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let timeout =
            sta.timer.schedule_event(zx::Time::after(1.seconds()), TimedEvent::Associating);
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
            )
            .expect_err("expected failure processing association response frame");

        // Verify timeout was canceled.
        assert_variant!(sta.timer.triggered(&timeout), None);

        // Verify MLME-ASSOCIATE.confirm message was sent.
        let msg = device.next_mlme_msg::<fidl_mlme::AssociateConfirm>().expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::AssociateConfirm {
                association_id: 0,
                result_code: fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified,
            }
        );
    }

    #[test]
    fn associating_timeout() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let timeout =
            sta.timer.schedule_event(zx::Time::after(1.seconds()), TimedEvent::Associating);
        let state = Associating { timeout };

        // Trigger timeout.
        state.on_timeout(&mut sta);

        // Verify timeout was canceled.
        assert_variant!(sta.timer.triggered(&timeout), None);

        // Verify MLME-ASSOCIATE.confirm message was sent.
        let msg = device.next_mlme_msg::<fidl_mlme::AssociateConfirm>().expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::AssociateConfirm {
                association_id: 0,
                result_code: fidl_mlme::AssociateResultCodes::RefusedTemporarily,
            }
        );
    }

    #[test]
    fn associating_deauthentication() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let timeout =
            sta.timer.schedule_event(zx::Time::after(1.seconds()), TimedEvent::Associating);
        let state = Associating { timeout };

        state.on_deauth_frame(
            &mut sta,
            &mac::DeauthHdr { reason_code: mac::ReasonCode::AP_INITIATED },
        );

        // Verify timeout was canceled.
        assert_variant!(sta.timer.triggered(&timeout), None);

        // Verify MLME-ASSOCIATE.confirm message was sent.
        let msg = device.next_mlme_msg::<fidl_mlme::AssociateConfirm>().expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::AssociateConfirm {
                association_id: 0,
                result_code: fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified,
            }
        );
    }

    #[test]
    fn associating_disassociation() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let timeout =
            sta.timer.schedule_event(zx::Time::after(1.seconds()), TimedEvent::Associating);
        let state = Associating { timeout };

        state.on_disassoc_frame(
            &mut sta,
            &mac::DisassocHdr { reason_code: mac::ReasonCode::AP_INITIATED },
        );

        // Verify timeout was canceled.
        assert_variant!(sta.timer.triggered(&timeout), None);

        // Verify MLME-ASSOCIATE.confirm message was sent.
        let msg = device.next_mlme_msg::<fidl_mlme::AssociateConfirm>().expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::AssociateConfirm {
                association_id: 0,
                result_code: fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified,
            }
        );
    }

    #[test]
    fn associated_deauthentication() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let state = Associated { controlled_port: ControlledPort::Open };

        state.on_deauth_frame(
            &mut sta,
            &mac::DeauthHdr { reason_code: mac::ReasonCode::AP_INITIATED },
        );

        // Verify MLME-ASSOCIATE.confirm message was sent.
        let msg =
            device.next_mlme_msg::<fidl_mlme::DeauthenticateIndication>().expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::DeauthenticateIndication {
                peer_sta_address: BSSID.0,
                reason_code: fidl_mlme::ReasonCode::ApInitiated,
            }
        );
    }

    #[test]
    fn associated_disassociation() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let state = Associated { controlled_port: ControlledPort::Open };

        state.on_disassoc_frame(
            &mut sta,
            &mac::DisassocHdr { reason_code: mac::ReasonCode::AP_INITIATED },
        );

        // Verify MLME-ASSOCIATE.confirm message was sent.
        let msg = device.next_mlme_msg::<fidl_mlme::DisassociateIndication>().expect("no message");
        assert_eq!(
            msg,
            fidl_mlme::DisassociateIndication {
                peer_sta_address: BSSID.0,
                reason_code: mac::ReasonCode::AP_INITIATED.0,
            }
        );
    }

    #[test]
    fn associated_move_data_closed_controlled_port() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let state = Associated { controlled_port: ControlledPort::Closed };

        let data_frame = make_data_frame_single_llc(None, None);
        let parsed_frame = mac::MacFrame::parse(&data_frame[..], false).expect("invalid frame");
        let (fixed, addr4, qos, body) = match parsed_frame {
            mac::MacFrame::Data { fixed_fields, addr4, qos_ctrl, body, .. } => {
                (fixed_fields, addr4.map(|x| *x), qos_ctrl.map(|x| x.get()), body)
            }
            _ => panic!("error parsing data frame"),
        };
        state.on_data_frame(&mut sta, &fixed, addr4, qos, body);

        // Verify data frame was dropped.
        assert_eq!(device.eth_queue.len(), 0);
    }

    #[test]
    fn associated_move_data_opened_controlled_port() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let state = Associated { controlled_port: ControlledPort::Open };

        let data_frame = make_data_frame_single_llc(None, None);
        let parsed_frame = mac::MacFrame::parse(&data_frame[..], false).expect("invalid frame");
        let (fixed, addr4, qos, body) = match parsed_frame {
            mac::MacFrame::Data { fixed_fields, addr4, qos_ctrl, body, .. } => {
                (fixed_fields, addr4.map(|x| *x), qos_ctrl.map(|x| x.get()), body)
            }
            _ => panic!("error parsing data frame"),
        };
        state.on_data_frame(&mut sta, &fixed, addr4, qos, body);

        // Verify data frame was processed.
        assert_eq!(device.eth_queue.len(), 1);
        #[rustfmt::skip]
        assert_eq!(device.eth_queue[0], [
            3, 3, 3, 3, 3, 3, // dst_addr
            4, 4, 4, 4, 4, 4, // src_addr
            9, 10, // ether_type
            11, 11, 11, // payload
        ]);
    }

    #[test]
    fn associated_handle_eapol_closed_controlled_port() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_protected_client_station(device.as_device(), scheduler.as_scheduler());
        let state = Associated { controlled_port: ControlledPort::Closed };

        let (src_addr, dst_addr, eapol_frame) = make_eapol_frame();
        let parsed_frame = mac::MacFrame::parse(&eapol_frame[..], false).expect("invalid frame");
        let (fixed, addr4, qos, body) = match parsed_frame {
            mac::MacFrame::Data { fixed_fields, addr4, qos_ctrl, body, .. } => {
                (fixed_fields, addr4.map(|x| *x), qos_ctrl.map(|x| x.get()), body)
            }
            _ => panic!("error parsing data frame"),
        };
        state.on_data_frame(&mut sta, &fixed, addr4, qos, body);

        // Verify EAPOL frame was not sent to netstack.
        assert_eq!(device.eth_queue.len(), 0);

        // Verify EAPoL frame was sent to SME.
        let eapol_ind = device
            .next_mlme_msg::<fidl_mlme::EapolIndication>()
            .expect("error reading EAPOL.indication");
        assert_eq!(
            eapol_ind,
            fidl_mlme::EapolIndication { src_addr, dst_addr, data: EAPOL_PDU.to_vec() }
        );
    }

    #[test]
    fn associated_handle_eapol_open_controlled_port() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_protected_client_station(device.as_device(), scheduler.as_scheduler());
        let state = Associated { controlled_port: ControlledPort::Open };

        let (src_addr, dst_addr, eapol_frame) = make_eapol_frame();
        let parsed_frame = mac::MacFrame::parse(&eapol_frame[..], false).expect("invalid frame");
        let (fixed, addr4, qos, body) = match parsed_frame {
            mac::MacFrame::Data { fixed_fields, addr4, qos_ctrl, body, .. } => {
                (fixed_fields, addr4.map(|x| *x), qos_ctrl.map(|x| x.get()), body)
            }
            _ => panic!("error parsing data frame"),
        };
        state.on_data_frame(&mut sta, &fixed, addr4, qos, body);

        // Verify EAPOL frame was not sent to netstack.
        assert_eq!(device.eth_queue.len(), 0);

        // Verify EAPoL frame was sent to SME.
        let eapol_ind = device
            .next_mlme_msg::<fidl_mlme::EapolIndication>()
            .expect("error reading EAPOL.indication");
        assert_eq!(
            eapol_ind,
            fidl_mlme::EapolIndication { src_addr, dst_addr, data: EAPOL_PDU.to_vec() }
        );
    }

    #[test]
    fn associated_handle_amsdus_open_controlled_port() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_protected_client_station(device.as_device(), scheduler.as_scheduler());
        let state = Associated { controlled_port: ControlledPort::Open };

        let data_frame = make_data_frame_amsdu();
        let parsed_frame = mac::MacFrame::parse(&data_frame[..], false).expect("invalid frame");
        let (fixed, addr4, qos, body) = match parsed_frame {
            mac::MacFrame::Data { fixed_fields, addr4, qos_ctrl, body, .. } => {
                (fixed_fields, addr4.map(|x| *x), qos_ctrl.map(|x| x.get()), body)
            }
            _ => panic!("error parsing data frame"),
        };
        state.on_data_frame(&mut sta, &fixed, addr4, qos, body);

        let queue = &device.eth_queue;
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
    fn state_transitions_joined_authing() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let mut state = States::new_initial();
        assert_variant!(state, States::Joined(_), "not in joined state");

        // Successful: Joined > Authenticating
        state = state.authenticate(&mut sta, 10);
        assert_variant!(state, States::Authenticating(_), "not in auth'ing state");
    }

    #[test]
    fn state_transitions_authing_success() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let mut state = States::from(statemachine::testing::new_state(Authenticating {
            timeout: EventId::default(),
        }));

        // Successful: Joined > Authenticating > Authenticated
        #[rustfmt::skip]
            let auth_resp_success = vec![
            // Mgmt Header:
            0b1011_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // Addr1
            7, 7, 7, 7, 7, 7, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Auth Header:
            0, 0, // Algorithm Number (Open)
            2, 0, // Txn Sequence Number
            0, 0, // Status Code
        ];
        state = state.on_mac_frame(&mut sta, &auth_resp_success[..], false);
        assert_variant!(state, States::Authenticated(_), "not in auth'ed state");
    }

    #[test]
    fn state_transitions_authing_failure() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let mut state = States::from(statemachine::testing::new_state(Authenticating {
            timeout: EventId::default(),
        }));

        // Failure: Joined > Authenticating > Joined
        #[rustfmt::skip]
        let auth_resp_failure = vec![
            // Mgmt Header:
            0b1011_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // Addr1
            7, 7, 7, 7, 7, 7, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Auth Header:
            0, 0, // Algorithm Number (Open)
            2, 0, // Txn Sequence Number
            42, 0, // Status Code
        ];
        state = state.on_mac_frame(&mut sta, &auth_resp_failure[..], false);
        assert_variant!(state, States::Joined(_), "not in joined state");
    }

    #[test]
    fn state_transitions_authing_timeout() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let mut state = States::new_initial();
        assert_variant!(state, States::Joined(_), "not in joined state");

        // Timeout: Joined > Authenticating > Joined
        state = state.authenticate(&mut sta, 10);
        let timeout_id = assert_variant!(state, States::Authenticating(ref state) => {
            state.timeout
        }, "not in auth'ing state");
        state = state.on_timed_event(&mut sta, timeout_id);
        assert_variant!(state, States::Joined(_), "not in joined state");
    }

    #[test]
    fn state_transitions_authing_deauth() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let mut state = States::from(statemachine::testing::new_state(Authenticating {
            timeout: EventId::default(),
        }));

        // Deauthenticate: Authenticating > Joined
        #[rustfmt::skip]
        let deauth = vec![
            // Mgmt Header:
            0b1100_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // Addr1
            7, 7, 7, 7, 7, 7, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Deauth Header:
            5, 0, // Algorithm Number (Open)
        ];
        state = state.on_mac_frame(&mut sta, &deauth[..], false);
        assert_variant!(state, States::Joined(_), "not in joined state");
    }

    #[test]
    fn state_transitions_authed() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let mut state = States::from(statemachine::testing::new_state(Authenticated));

        // Deauthenticate: Authenticated > Joined
        #[rustfmt::skip]
        let deauth = vec![
            // Mgmt Header:
            0b1100_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // Addr1
            7, 7, 7, 7, 7, 7, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Deauth Header:
            5, 0, // Algorithm Number (Open)
        ];
        state = state.on_mac_frame(&mut sta, &deauth[..], false);
        assert_variant!(state, States::Joined(_), "not in joined state");
    }

    #[test]
    fn state_transitions_foreign_auth_resp() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let mut state = States::from(statemachine::testing::new_state(Authenticating {
            timeout: EventId::default(),
        }));

        // Send foreign auth response. State should not change.
        #[rustfmt::skip]
        let auth_resp_success = vec![
            // Mgmt Header:
            0b1011_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            5, 5, 5, 5, 5, 5, // Addr1
            7, 7, 7, 7, 7, 7, // Addr2
            5, 5, 5, 5, 5, 5, // Addr3
            0x10, 0, // Sequence Control
            // Auth Header:
            0, 0, // Algorithm Number (Open)
            2, 0, // Txn Sequence Number
            0, 0, // Status Code
        ];
        state = state.on_mac_frame(&mut sta, &auth_resp_success[..], false);
        assert_variant!(state, States::Authenticating(_), "not in auth'ing state");

        // Verify that an authentication response from the joined BSS still moves the Client
        // forward.
        #[rustfmt::skip]
        let auth_resp_success = vec![
            // Mgmt Header:
            0b1011_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // Addr1
            7, 7, 7, 7, 7, 7, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Auth Header:
            0, 0, // Algorithm Number (Open)
            2, 0, // Txn Sequence Number
            0, 0, // Status Code
        ];
        state = state.on_mac_frame(&mut sta, &auth_resp_success[..], false);
        assert_variant!(state, States::Authenticated(_), "not in auth'ed state");
    }

    #[test]
    fn state_transitions_associng_success() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let mut state = States::from(statemachine::testing::new_state(Associating {
            timeout: EventId::default(),
        }));

        // Successful: Associating > Associated
        #[rustfmt::skip]
        let assoc_resp_success = vec![
            // Mgmt Header:
            0b0001_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // Addr1
            7, 7, 7, 7, 7, 7, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Assoc Resp Header:
            0, 0, // Capabilities
            0, 0, // Status Code
            0, 0, // AID
        ];
        state = state.on_mac_frame(&mut sta, &assoc_resp_success[..], false);
        assert_variant!(state, States::Associated(_), "not in associated state");
    }

    #[test]
    fn state_transitions_associng_failure() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let mut state = States::from(statemachine::testing::new_state(Associating {
            timeout: EventId::default(),
        }));

        // Failure: Associating > Associated
        #[rustfmt::skip]
        let assoc_resp_success = vec![
            // Mgmt Header:
            0b0001_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // Addr1
            7, 7, 7, 7, 7, 7, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Assoc Resp Header:
            0, 0, // Capabilities
            2, 0, // Status Code
            0, 0, // AID
        ];
        state = state.on_mac_frame(&mut sta, &assoc_resp_success[..], false);
        assert_variant!(state, States::Authenticated(_), "not in authenticated state");
    }

    #[test]
    fn state_transitions_associng_timeout() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let mut state = States::from(statemachine::testing::new_state(Authenticated));

        state = state.associate(&mut sta);
        let timeout_id = assert_variant!(state, States::Associating(ref state) => {
            state.timeout.clone()
        }, "not in assoc'ing state");
        state = state.on_timed_event(&mut sta, timeout_id);
        assert_variant!(state, States::Authenticated(_), "not in authenticated state");
    }

    #[test]
    fn state_transitions_associng_deauthing() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let mut state = States::from(statemachine::testing::new_state(Associating {
            timeout: EventId::default(),
        }));

        // Deauthentication: Associating > Joined
        #[rustfmt::skip]
        let deauth = vec![
            // Mgmt Header:
            0b1100_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // Addr1
            7, 7, 7, 7, 7, 7, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Deauth Header:
            4, 0, // Reason Code
        ];
        state = state.on_mac_frame(&mut sta, &deauth[..], false);
        assert_variant!(state, States::Joined(_), "not in joined state");
    }

    #[test]
    fn state_transitions_assoced_disassoc() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let mut state = States::from(statemachine::testing::new_state(Associated {
            controlled_port: ControlledPort::Open,
        }));

        // Disassociation: Associating > Authenticated
        #[rustfmt::skip]
        let disassoc = vec![
            // Mgmt Header:
            0b1010_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // Addr1
            7, 7, 7, 7, 7, 7, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Deauth Header:
            4, 0, // Reason Code
        ];
        state = state.on_mac_frame(&mut sta, &disassoc[..], false);
        assert_variant!(state, States::Authenticated(_), "not in authenticated state");
    }

    #[test]
    fn state_transitions_assoced_deauthing() {
        let mut device = FakeDevice::new();
        let mut scheduler = FakeScheduler::new();
        let mut sta = make_client_station(device.as_device(), scheduler.as_scheduler());
        let mut state = States::from(statemachine::testing::new_state(Associated {
            controlled_port: ControlledPort::Open,
        }));

        // Deauthentication: Associated > Joined
        #[rustfmt::skip]
        let deauth = vec![
            // Mgmt Header:
            0b1100_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // Addr1
            7, 7, 7, 7, 7, 7, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Deauth Header:
            4, 0, // Reason Code
        ];
        state = state.on_mac_frame(&mut sta, &deauth[..], false);
        assert_variant!(state, States::Joined(_), "not in joined state");
    }
}
