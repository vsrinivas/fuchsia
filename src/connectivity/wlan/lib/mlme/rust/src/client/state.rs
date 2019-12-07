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
        client::{Client, Context, TimedEvent},
        timer::*,
    },
    fidl_fuchsia_wlan_mlme as fidl_mlme,
    log::{error, info, warn},
    wlan_common::{
        mac::{self, MacAddr},
        time::TimeUnit,
    },
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
    fn on_deauth_frame(&self, sta: &mut Client, ctx: &mut Context, deauth_hdr: &mac::DeauthHdr) {
        let reason_code = fidl_mlme::ReasonCode::from_primitive(deauth_hdr.reason_code.0)
            .unwrap_or(fidl_mlme::ReasonCode::UnspecifiedReason);
        sta.send_deauthenticate_ind(ctx, reason_code);
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
    fn authenticate(
        &self,
        sta: &mut Client,
        ctx: &mut Context,
        timeout_bcn_count: u8,
    ) -> Result<EventId, ()> {
        match sta.send_open_auth_frame(ctx) {
            Ok(()) => {
                let duration_tus = TimeUnit::DEFAULT_BEACON_INTERVAL * timeout_bcn_count;
                let deadline = ctx.timer.now() + duration_tus.into();
                let event = TimedEvent::Authenticating;
                let event_id = ctx.timer.schedule_event(deadline, event);
                Ok(event_id)
            }
            Err(e) => {
                error!("{}", e);
                sta.send_authenticate_conf(ctx, fidl_mlme::AuthenticateResultCodes::Refused);
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
    fn on_auth_frame(
        &self,
        sta: &mut Client,
        ctx: &mut Context,
        auth_hdr: &mac::AuthHdr,
    ) -> Result<(), ()> {
        ctx.timer.cancel_event(self.timeout);

        match auth::is_valid_open_ap_resp(auth_hdr) {
            Ok(()) => {
                sta.send_authenticate_conf(ctx, fidl_mlme::AuthenticateResultCodes::Success);
                Ok(())
            }
            Err(e) => {
                error!("authentication with BSS failed: {}", e);
                sta.send_authenticate_conf(ctx, fidl_mlme::AuthenticateResultCodes::Refused);
                Err(())
            }
        }
    }

    /// Invoked when the pending timeout fired. The original authentication request is now
    /// considered to be expired and invalid - the authentication failed. As a consequence,
    /// an MLME-AUTHENTICATION.confirm message is reported to MLME's SME peer indicating the
    /// timeout.
    fn on_timeout(&self, sta: &mut Client, ctx: &mut Context) {
        // At this point, the event should already be canceled by the state's owner. However,
        // ensure the timeout is canceled in any case.
        ctx.timer.cancel_event(self.timeout);

        sta.send_authenticate_conf(ctx, fidl_mlme::AuthenticateResultCodes::AuthFailureTimeout);
    }
}

impl DeauthenticationHandler for Authenticating {
    /// Processes an inbound deauthentication frame.
    /// This always results in an MLME-AUTHENTICATE.confirm message to MLME's SME peer.
    /// The pending authentication timeout will be canceled in this process.
    fn on_deauth_frame(&self, sta: &mut Client, ctx: &mut Context, deauth_hdr: &mac::DeauthHdr) {
        ctx.timer.cancel_event(self.timeout);

        info!(
            "received spurious deauthentication frame while authenticating with BSS (unusual); \
             authentication failed: {:?}",
            { deauth_hdr.reason_code }
        );
        sta.send_authenticate_conf(ctx, fidl_mlme::AuthenticateResultCodes::Refused);
    }
}

/// Client received a "successful" authentication response from the BSS.
pub struct Authenticated;
impl DeauthenticationHandler for Authenticated {}

impl Authenticated {
    /// Initiates an association with the currently joined BSS.
    /// Returns Ok(timeout) if association request was sent successfully.
    /// Otherwise an Err(()) is returned and an ASSOCIATE.confirm message to its SME peer.
    fn associate(&self, sta: &mut Client, ctx: &mut Context) -> Result<EventId, ()> {
        // TODO(eyw) Send actual IEs.
        let ssid = vec![];
        let rates = vec![1, 2, 8];
        let rsne = vec![];
        let ht_cap = vec![];
        let vht_cap = vec![];
        match sta.send_assoc_req_frame(
            ctx,
            0,
            &ssid[..],
            &rates[..],
            &rsne[..],
            &ht_cap[..],
            &vht_cap[..],
        ) {
            Ok(()) => {
                let duration_tus = TimeUnit::DEFAULT_BEACON_INTERVAL * ASSOC_TIMEOUT_BCN_PERIODS;
                let deadline = ctx.timer.now() + duration_tus.into();
                let event = TimedEvent::Associating;
                let event_id = ctx.timer.schedule_event(deadline, event);
                Ok(event_id)
            }
            Err(e) => {
                error!("{}", e);
                sta.send_associate_conf(
                    ctx,
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
    fn on_deauth_frame(&self, sta: &mut Client, ctx: &mut Context, deauth_hdr: &mac::DeauthHdr) {
        ctx.timer.cancel_event(self.timeout);

        info!(
            "received spurious deauthentication frame while associating with BSS (unusual); \
             association failed: {:?}",
            { deauth_hdr.reason_code }
        );
        sta.send_associate_conf(
            ctx,
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
        ctx: &mut Context,
        assoc_resp_hdr: &mac::AssocRespHdr,
    ) -> Result<(mac::Aid, ControlledPort), ()> {
        ctx.timer.cancel_event(self.timeout);

        match assoc_resp_hdr.status_code {
            mac::StatusCode::SUCCESS => {
                sta.send_associate_conf(
                    ctx,
                    assoc_resp_hdr.aid,
                    fidl_mlme::AssociateResultCodes::Success,
                );
                let controlled_port =
                    if sta.is_rsn { ControlledPort::Closed } else { ControlledPort::Open };
                Ok((assoc_resp_hdr.aid, controlled_port))
            }
            status_code => {
                error!("association with BSS failed: {:?}", status_code);
                sta.send_associate_conf(
                    ctx,
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
    fn on_disassoc_frame(
        &self,
        sta: &mut Client,
        ctx: &mut Context,
        _disassoc_hdr: &mac::DisassocHdr,
    ) {
        ctx.timer.cancel_event(self.timeout);

        warn!("received unexpected disassociation frame while associating");
        sta.send_associate_conf(
            ctx,
            FAILED_ASSOCIATION_AID,
            fidl_mlme::AssociateResultCodes::RefusedReasonUnspecified,
        );
    }

    /// Invoked when the pending timeout fired. The original association request is now
    /// considered to be expired and invalid - the association failed. As a consequence,
    /// an MLME-ASSOCIATE.confirm message is reported to MLME's SME peer indicating the
    /// timeout.
    fn on_timeout(&self, sta: &mut Client, ctx: &mut Context) {
        // At this point, the event should already be canceled by the state's owner. However,
        // ensure the timeout is canceled in any case.
        ctx.timer.cancel_event(self.timeout);

        sta.send_associate_conf(
            ctx,
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
    aid: mac::Aid,
    controlled_port: ControlledPort,
}
impl DeauthenticationHandler for Associated {}

impl Associated {
    /// Processes inbound data frames.
    // TODO(42159): Drop frames from foreign BSS.
    fn on_data_frame<B: ByteSlice>(
        &self,
        sta: &mut Client,
        ctx: &mut Context,
        fixed_data_fields: &mac::FixedDataHdrFields,
        addr4: Option<mac::Addr4>,
        qos_ctrl: Option<mac::QosControl>,
        body: B,
    ) {
        self.request_bu_if_available(
            sta,
            ctx,
            fixed_data_fields.frame_ctrl,
            mac::data_dst_addr(fixed_data_fields),
        );

        let is_controlled_port_open = self.controlled_port.is_opened();
        sta.handle_data_frame(
            ctx,
            fixed_data_fields,
            addr4,
            qos_ctrl,
            body,
            is_controlled_port_open,
        );
    }

    /// Process every inbound management frame before its being handed off to a more specific
    /// handler.
    fn on_any_mgmt_frame(&self, sta: &mut Client, ctx: &mut Context, mgmt_hdr: &mac::MgmtHdr) {
        self.request_bu_if_available(sta, ctx, mgmt_hdr.frame_ctrl, mgmt_hdr.addr1);
    }

    /// Sends PS-POLL requests if the FrameControl's more_data bit is set, and the received frame
    /// was addressed for this STA. No-op if the controlled port is closed.
    fn request_bu_if_available(
        &self,
        sta: &mut Client,
        ctx: &mut Context,
        fc: mac::FrameControl,
        dst_addr: MacAddr,
    ) {
        if !self.controlled_port.is_opened() {
            return;
        }
        // IEEE Std. 802.11-2016, 9.2.4.1.8
        if fc.more_data() && dst_addr == sta.iface_mac {
            let _ignored = sta.send_ps_poll_frame(ctx, self.aid);
        }
    }

    /// Processes an inbound diassociation frame.
    /// This always results in an MLME-DISASSOCIATE.indication message to MLME's SME peer.
    fn on_disassoc_frame(
        &self,
        sta: &mut Client,
        ctx: &mut Context,
        disassoc_hdr: &mac::DisassocHdr,
    ) {
        let reason_code = fidl_mlme::ReasonCode::from_primitive(disassoc_hdr.reason_code.0)
            .unwrap_or(fidl_mlme::ReasonCode::UnspecifiedReason);
        sta.send_disassoc_ind(ctx, reason_code);
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
    pub fn authenticate(
        self,
        sta: &mut Client,
        ctx: &mut Context,
        timeout_bcn_count: u8,
    ) -> States {
        match self {
            // MLME-AUTHENTICATE.request messages are only processed when the Client is "Joined".
            States::Joined(state) => match state.authenticate(sta, ctx, timeout_bcn_count) {
                Ok(timeout) => state.transition_to(Authenticating { timeout }).into(),
                Err(()) => state.into(),
            },
            // Reject MLME-AUTHENTICATE.request if STA is not in "Joined" state.
            _ => {
                error!("received MLME-AUTHENTICATE.request in invalid state");
                sta.send_authenticate_conf(ctx, fidl_mlme::AuthenticateResultCodes::Refused);
                self
            }
        }
    }

    /// Initates an association with the joined BSS.
    // TODO(eyw): Carry MLME-ASSOCIATE.request information to construct correct association frame.
    pub fn associate(self, sta: &mut Client, ctx: &mut Context) -> States {
        match self {
            States::Authenticated(state) => match state.associate(sta, ctx) {
                Ok(timeout) => state.transition_to(Associating { timeout }).into(),
                Err(()) => state.into(),
            },
            _ => {
                error!("received MLME-ASSOCIATE.request in invalid state");
                sta.send_associate_conf(
                    ctx,
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
        ctx: &mut Context,
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
            mac::MacFrame::Mgmt { mgmt_hdr, body, .. } => {
                self.on_mgmt_frame(sta, ctx, &mgmt_hdr, body)
            }
            mac::MacFrame::Data { fixed_fields, addr4, qos_ctrl, body, .. } => {
                // Drop frames from foreign BSS.
                match mac::data_bssid(&fixed_fields) {
                    Some(bssid) if bssid == sta.bssid.0 => (),
                    _ => return self,
                };

                if let States::Associated(state) = &self {
                    state.on_data_frame(
                        sta,
                        ctx,
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
        ctx: &mut Context,
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
                    match state.on_auth_frame(sta, ctx, &auth_hdr) {
                        Ok(()) => state.transition_to(Authenticated).into(),
                        Err(()) => state.transition_to(Joined).into(),
                    }
                }
                mac::MgmtBody::Deauthentication { deauth_hdr, .. } => {
                    state.on_deauth_frame(sta, ctx, &deauth_hdr);
                    state.transition_to(Joined).into()
                }
                _ => state.into(),
            },
            States::Authenticated(state) => match mgmt_body {
                mac::MgmtBody::Deauthentication { deauth_hdr, .. } => {
                    state.on_deauth_frame(sta, ctx, &deauth_hdr);
                    state.transition_to(Joined).into()
                }
                _ => state.into(),
            },
            States::Associating(state) => match mgmt_body {
                mac::MgmtBody::AssociationResp { assoc_resp_hdr, .. } => {
                    match state.on_assoc_resp_frame(sta, ctx, &assoc_resp_hdr) {
                        Ok((aid, controlled_port)) => {
                            state.transition_to(Associated { aid, controlled_port }).into()
                        }
                        Err(()) => state.transition_to(Authenticated).into(),
                    }
                }
                mac::MgmtBody::Deauthentication { deauth_hdr, .. } => {
                    state.on_deauth_frame(sta, ctx, &deauth_hdr);
                    state.transition_to(Joined).into()
                }
                // This case is highly unlikely and only added to improve interoperability with
                // buggy Access Points.
                mac::MgmtBody::Disassociation { disassoc_hdr, .. } => {
                    state.on_disassoc_frame(sta, ctx, &disassoc_hdr);
                    state.transition_to(Authenticated).into()
                }
                _ => state.into(),
            },
            States::Associated(state) => {
                state.on_any_mgmt_frame(sta, ctx, mgmt_hdr);
                match mgmt_body {
                    mac::MgmtBody::Deauthentication { deauth_hdr, .. } => {
                        state.on_deauth_frame(sta, ctx, &deauth_hdr);
                        state.transition_to(Joined).into()
                    }
                    mac::MgmtBody::Disassociation { disassoc_hdr, .. } => {
                        state.on_disassoc_frame(sta, ctx, &disassoc_hdr);
                        state.transition_to(Authenticated).into()
                    }
                    _ => state.into(),
                }
            }
            _ => self,
        }
    }

    /// Callback when a previously scheduled event fired.
    pub fn on_timed_event(self, sta: &mut Client, ctx: &mut Context, event_id: EventId) -> States {
        // Lookup the event matching the given id.
        let event = match ctx.timer.triggered(&event_id) {
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
                    state.on_timeout(sta, ctx);
                    state.transition_to(Joined).into()
                }
                _ => {
                    error!("received Authenticating timeout in unexpected state; ignoring timeout");
                    self
                }
            },
            TimedEvent::Associating => match self {
                States::Associating(state) => {
                    state.on_timeout(sta, ctx);
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
            client::{cpp_proxy::FakeCppChannelScheduler, ClientConfig},
            device::{Device, FakeDevice},
        },
        fuchsia_zircon::{self as zx, DurationNum},
        wlan_common::{
            assert_variant,
            mac::{Bssid, MacAddr},
            sequence::SequenceManager,
            test_utils::fake_frames::*,
        },
        wlan_statemachine as statemachine,
    };

    const BSSID: Bssid = Bssid([6u8; 6]);
    const IFACE_MAC: MacAddr = [3u8; 6];

    struct MockObjects {
        fake_device: FakeDevice,
        fake_scheduler: FakeScheduler,
        fake_chan_sched: FakeCppChannelScheduler,
    }

    impl MockObjects {
        fn new() -> Self {
            Self {
                fake_device: FakeDevice::new(),
                fake_scheduler: FakeScheduler::new(),
                fake_chan_sched: FakeCppChannelScheduler::new(),
            }
        }

        fn make_ctx(&mut self) -> Context {
            let device = self.fake_device.as_device();
            self.make_ctx_with_device(device)
        }

        fn make_ctx_with_device(&mut self, device: Device) -> Context {
            let timer = Timer::<TimedEvent>::new(self.fake_scheduler.as_scheduler());
            Context {
                config: ClientConfig {
                    signal_report_beacon_timeout: 99999,
                    ensure_on_channel_time: 0,
                },
                device,
                buf_provider: FakeBufferProvider::new(),
                timer,
                seq_mgr: SequenceManager::new(),
                cpp_chan_sched: self.fake_chan_sched.as_chan_sched(),
            }
        }
    }

    fn make_client_station() -> Client {
        Client::new(BSSID, IFACE_MAC, false)
    }

    fn make_protected_client_station() -> Client {
        Client::new(BSSID, IFACE_MAC, true)
    }

    #[test]
    fn join_state_authenticate_success() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let state = Joined;
        let timeout_id = state.authenticate(&mut sta, &mut ctx, 10).expect("failed authenticating");

        // Verify an event was queued up in the timer.
        assert_variant!(ctx.timer.triggered(&timeout_id), Some(TimedEvent::Authenticating));

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

        let state = Joined;
        state.authenticate(&mut sta, &mut ctx, 10).expect_err("should fail authenticating");

        // Verify no event was queued up in the timer.
        assert_eq!(ctx.timer.scheduled_event_count(), 0);

        // Verify MLME-AUTHENTICATE.confirm message was sent.
        let msg =
            m.fake_device.next_mlme_msg::<fidl_mlme::AuthenticateConfirm>().expect("no message");
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
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let timeout =
            ctx.timer.schedule_event(zx::Time::after(1.seconds()), TimedEvent::Authenticating);
        let state = Authenticating { timeout };

        // Verify authentication was considered successful.
        state
            .on_auth_frame(
                &mut sta,
                &mut ctx,
                &mac::AuthHdr {
                    auth_alg_num: mac::AuthAlgorithmNumber::OPEN,
                    auth_txn_seq_num: 2,
                    status_code: mac::StatusCode::SUCCESS,
                },
            )
            .expect("failed processing auth frame");

        // Verify timeout was canceled.
        assert_variant!(ctx.timer.triggered(&timeout), None);

        // Verify MLME-AUTHENTICATE.confirm message was sent.
        let msg =
            m.fake_device.next_mlme_msg::<fidl_mlme::AuthenticateConfirm>().expect("no message");
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
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let timeout =
            ctx.timer.schedule_event(zx::Time::after(1.seconds()), TimedEvent::Authenticating);
        let state = Authenticating { timeout };

        // Verify authentication was considered successful.
        state
            .on_auth_frame(
                &mut sta,
                &mut ctx,
                &mac::AuthHdr {
                    auth_alg_num: mac::AuthAlgorithmNumber::OPEN,
                    auth_txn_seq_num: 2,
                    status_code: mac::StatusCode::NOT_IN_SAME_BSS,
                },
            )
            .expect_err("expected failure processing auth frame");

        // Verify timeout was canceled.
        assert_variant!(ctx.timer.triggered(&timeout), None);

        // Verify MLME-AUTHENTICATE.confirm message was sent.
        let msg =
            m.fake_device.next_mlme_msg::<fidl_mlme::AuthenticateConfirm>().expect("no message");
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
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let timeout =
            ctx.timer.schedule_event(zx::Time::after(1.seconds()), TimedEvent::Authenticating);
        let state = Authenticating { timeout };

        state.on_timeout(&mut sta, &mut ctx);

        // Verify timeout was canceled.
        assert_variant!(ctx.timer.triggered(&timeout), None);

        // Verify MLME-AUTHENTICATE.confirm message was sent.
        let msg =
            m.fake_device.next_mlme_msg::<fidl_mlme::AuthenticateConfirm>().expect("no message");
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
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let timeout =
            ctx.timer.schedule_event(zx::Time::after(1.seconds()), TimedEvent::Authenticating);
        let state = Authenticating { timeout };

        state.on_deauth_frame(
            &mut sta,
            &mut ctx,
            &mac::DeauthHdr { reason_code: mac::ReasonCode::NO_MORE_STAS },
        );

        // Verify timeout was canceled.
        assert_variant!(ctx.timer.triggered(&timeout), None);

        // Verify MLME-AUTHENTICATE.confirm message was sent.
        let msg =
            m.fake_device.next_mlme_msg::<fidl_mlme::AuthenticateConfirm>().expect("no message");
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
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let state = Authenticated;

        state.on_deauth_frame(
            &mut sta,
            &mut ctx,
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
            }
        );
    }

    #[test]
    fn associating_success_unprotected() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let timeout =
            ctx.timer.schedule_event(ctx.timer.now() + 1.seconds(), TimedEvent::Associating);
        let state = Associating { timeout };

        let (aid, controlled_port) = state
            .on_assoc_resp_frame(
                &mut sta,
                &mut ctx,
                &mac::AssocRespHdr {
                    aid: 42,
                    capabilities: mac::CapabilityInfo(52),
                    status_code: mac::StatusCode::SUCCESS,
                },
            )
            .expect("failed processing association response frame");
        assert_eq!(aid, 42);
        assert_eq!(ControlledPort::Open, controlled_port);

        // Verify timeout was canceled.
        assert_variant!(ctx.timer.triggered(&timeout), None);

        // Verify MLME-ASSOCIATE.confirm message was sent.
        let msg = m.fake_device.next_mlme_msg::<fidl_mlme::AssociateConfirm>().expect("no message");
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
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_protected_client_station();
        let timeout =
            ctx.timer.schedule_event(zx::Time::after(1.seconds()), TimedEvent::Associating);
        let state = Associating { timeout };

        let (aid, controlled_port) = state
            .on_assoc_resp_frame(
                &mut sta,
                &mut ctx,
                &mac::AssocRespHdr {
                    aid: 42,
                    capabilities: mac::CapabilityInfo(52),
                    status_code: mac::StatusCode::SUCCESS,
                },
            )
            .expect("failed processing association response frame");
        assert_eq!(aid, 42);
        assert_eq!(ControlledPort::Closed, controlled_port);

        // Verify timeout was canceled.
        assert_variant!(ctx.timer.triggered(&timeout), None);

        // Verify MLME-ASSOCIATE.confirm message was sent.
        let msg = m.fake_device.next_mlme_msg::<fidl_mlme::AssociateConfirm>().expect("no message");
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
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let timeout =
            ctx.timer.schedule_event(zx::Time::after(1.seconds()), TimedEvent::Associating);
        let state = Associating { timeout };

        // Verify authentication was considered successful.
        state
            .on_assoc_resp_frame(
                &mut sta,
                &mut ctx,
                &mac::AssocRespHdr {
                    aid: 42,
                    capabilities: mac::CapabilityInfo(52),
                    status_code: mac::StatusCode::NOT_IN_SAME_BSS,
                },
            )
            .expect_err("expected failure processing association response frame");

        // Verify timeout was canceled.
        assert_variant!(ctx.timer.triggered(&timeout), None);

        // Verify MLME-ASSOCIATE.confirm message was sent.
        let msg = m.fake_device.next_mlme_msg::<fidl_mlme::AssociateConfirm>().expect("no message");
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
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let timeout =
            ctx.timer.schedule_event(zx::Time::after(1.seconds()), TimedEvent::Associating);
        let state = Associating { timeout };

        // Trigger timeout.
        state.on_timeout(&mut sta, &mut ctx);

        // Verify timeout was canceled.
        assert_variant!(ctx.timer.triggered(&timeout), None);

        // Verify MLME-ASSOCIATE.confirm message was sent.
        let msg = m.fake_device.next_mlme_msg::<fidl_mlme::AssociateConfirm>().expect("no message");
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
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let timeout =
            ctx.timer.schedule_event(zx::Time::after(1.seconds()), TimedEvent::Associating);
        let state = Associating { timeout };

        state.on_deauth_frame(
            &mut sta,
            &mut ctx,
            &mac::DeauthHdr { reason_code: mac::ReasonCode::AP_INITIATED },
        );

        // Verify timeout was canceled.
        assert_variant!(ctx.timer.triggered(&timeout), None);

        // Verify MLME-ASSOCIATE.confirm message was sent.
        let msg = m.fake_device.next_mlme_msg::<fidl_mlme::AssociateConfirm>().expect("no message");
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
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let timeout =
            ctx.timer.schedule_event(zx::Time::after(1.seconds()), TimedEvent::Associating);
        let state = Associating { timeout };

        state.on_disassoc_frame(
            &mut sta,
            &mut ctx,
            &mac::DisassocHdr { reason_code: mac::ReasonCode::AP_INITIATED },
        );

        // Verify timeout was canceled.
        assert_variant!(ctx.timer.triggered(&timeout), None);

        // Verify MLME-ASSOCIATE.confirm message was sent.
        let msg = m.fake_device.next_mlme_msg::<fidl_mlme::AssociateConfirm>().expect("no message");
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
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let state = Associated { aid: 0, controlled_port: ControlledPort::Open };

        state.on_deauth_frame(
            &mut sta,
            &mut ctx,
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
            }
        );
    }

    #[test]
    fn associated_disassociation() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let state = Associated { aid: 0, controlled_port: ControlledPort::Open };

        state.on_disassoc_frame(
            &mut sta,
            &mut ctx,
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
            }
        );
    }

    #[test]
    fn associated_move_data_closed_controlled_port() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let state = Associated { aid: 0, controlled_port: ControlledPort::Closed };

        let data_frame = make_data_frame_single_llc(None, None);
        let (fixed, addr4, qos, body) = parse_data_frame(&data_frame[..]);
        state.on_data_frame(&mut sta, &mut ctx, &fixed, addr4, qos, body);

        // Verify data frame was dropped.
        assert_eq!(m.fake_device.eth_queue.len(), 0);
    }

    #[test]
    fn associated_move_data_opened_controlled_port() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let state = Associated { aid: 0, controlled_port: ControlledPort::Open };

        let data_frame = make_data_frame_single_llc(None, None);
        let (fixed, addr4, qos, body) = parse_data_frame(&data_frame[..]);
        state.on_data_frame(&mut sta, &mut ctx, &fixed, addr4, qos, body);

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
        let state = Associated { aid: 0, controlled_port: ControlledPort::Closed };

        let (src_addr, dst_addr, eapol_frame) = make_eapol_frame();
        let (fixed, addr4, qos, body) = parse_data_frame(&eapol_frame[..]);
        state.on_data_frame(&mut sta, &mut ctx, &fixed, addr4, qos, body);

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
        let state = Associated { aid: 0, controlled_port: ControlledPort::Open };

        let (src_addr, dst_addr, eapol_frame) = make_eapol_frame();
        let (fixed, addr4, qos, body) = parse_data_frame(&eapol_frame[..]);
        state.on_data_frame(&mut sta, &mut ctx, &fixed, addr4, qos, body);

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
        let state = Associated { aid: 0, controlled_port: ControlledPort::Open };

        let data_frame = make_data_frame_amsdu();
        let (fixed, addr4, qos, body) = parse_data_frame(&data_frame[..]);
        state.on_data_frame(&mut sta, &mut ctx, &fixed, addr4, qos, body);

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
        let state = Associated { aid: 42, controlled_port: ControlledPort::Open };

        let data_frame = make_data_frame_single_llc(None, None);
        let (mut fixed, addr4, qos, body) = parse_data_frame(&data_frame[..]);
        fixed.frame_ctrl = fixed.frame_ctrl.with_more_data(true);
        state.on_data_frame(&mut sta, &mut ctx, &fixed, addr4, qos, body);

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
        let state = Associated { aid: 42, controlled_port: ControlledPort::Open };

        state.on_any_mgmt_frame(
            &mut sta,
            &mut ctx,
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

        // Closed Controlled port
        let state = Associated { aid: 42, controlled_port: ControlledPort::Closed };
        let data_frame = make_data_frame_single_llc(None, None);
        let (mut fixed, addr4, qos, body) = parse_data_frame(&data_frame[..]);
        fixed.frame_ctrl = fixed.frame_ctrl.with_more_data(true);
        state.on_data_frame(&mut sta, &mut ctx, &fixed, addr4, qos, body);
        assert_eq!(m.fake_device.wlan_queue.len(), 0);

        // Foreign management frame
        let state = States::from(statemachine::testing::new_state(Associated {
            aid: 42,
            controlled_port: ControlledPort::Open,
        }));
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
        state.on_mac_frame(&mut sta, &mut ctx, &beacon[..], false);
        assert_eq!(m.fake_device.wlan_queue.len(), 0);
    }

    #[test]
    fn associated_drop_foreign_data_frames() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();

        // Foreign data frame
        let state = States::from(statemachine::testing::new_state(Associated {
            aid: 42,
            controlled_port: ControlledPort::Open,
        }));
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
        state.on_mac_frame(&mut sta, &mut ctx, &bytes[..], false);
        assert_eq!(m.fake_device.eth_queue.len(), 0);
    }

    #[test]
    fn state_transitions_joined_authing() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut state = States::new_initial();
        assert_variant!(state, States::Joined(_), "not in joined state");

        // Successful: Joined > Authenticating
        state = state.authenticate(&mut sta, &mut ctx, 10);
        assert_variant!(state, States::Authenticating(_), "not in auth'ing state");
    }

    #[test]
    fn state_transitions_authing_success() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
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
            3, 3, 3, 3, 3, 3, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Auth Header:
            0, 0, // Algorithm Number (Open)
            2, 0, // Txn Sequence Number
            0, 0, // Status Code
        ];
        state = state.on_mac_frame(&mut sta, &mut ctx, &auth_resp_success[..], false);
        assert_variant!(state, States::Authenticated(_), "not in auth'ed state");
    }

    #[test]
    fn state_transitions_authing_failure() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
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
            3, 3, 3, 3, 3, 3, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Auth Header:
            0, 0, // Algorithm Number (Open)
            2, 0, // Txn Sequence Number
            42, 0, // Status Code
        ];
        state = state.on_mac_frame(&mut sta, &mut ctx, &auth_resp_failure[..], false);
        assert_variant!(state, States::Joined(_), "not in joined state");
    }

    #[test]
    fn state_transitions_authing_timeout() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut state = States::new_initial();
        assert_variant!(state, States::Joined(_), "not in joined state");

        // Timeout: Joined > Authenticating > Joined
        state = state.authenticate(&mut sta, &mut ctx, 10);
        let timeout_id = assert_variant!(state, States::Authenticating(ref state) => {
            state.timeout
        }, "not in auth'ing state");
        state = state.on_timed_event(&mut sta, &mut ctx, timeout_id);
        assert_variant!(state, States::Joined(_), "not in joined state");
    }

    #[test]
    fn state_transitions_authing_deauth() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
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
            3, 3, 3, 3, 3, 3, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Deauth Header:
            5, 0, // Algorithm Number (Open)
        ];
        state = state.on_mac_frame(&mut sta, &mut ctx, &deauth[..], false);
        assert_variant!(state, States::Joined(_), "not in joined state");
    }

    #[test]
    fn state_transitions_authed() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut state = States::from(statemachine::testing::new_state(Authenticated));

        // Deauthenticate: Authenticated > Joined
        #[rustfmt::skip]
        let deauth = vec![
            // Mgmt Header:
            0b1100_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // Addr1
            3, 3, 3, 3, 3, 3, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Deauth Header:
            5, 0, // Algorithm Number (Open)
        ];
        state = state.on_mac_frame(&mut sta, &mut ctx, &deauth[..], false);
        assert_variant!(state, States::Joined(_), "not in joined state");
    }

    #[test]
    fn state_transitions_foreign_auth_resp() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
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
            3, 3, 3, 3, 3, 3, // Addr2
            5, 5, 5, 5, 5, 5, // Addr3
            0x10, 0, // Sequence Control
            // Auth Header:
            0, 0, // Algorithm Number (Open)
            2, 0, // Txn Sequence Number
            0, 0, // Status Code
        ];
        state = state.on_mac_frame(&mut sta, &mut ctx, &auth_resp_success[..], false);
        assert_variant!(state, States::Authenticating(_), "not in auth'ing state");

        // Verify that an authentication response from the joined BSS still moves the Client
        // forward.
        #[rustfmt::skip]
        let auth_resp_success = vec![
            // Mgmt Header:
            0b1011_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // Addr1
            3, 3, 3, 3, 3, 3, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Auth Header:
            0, 0, // Algorithm Number (Open)
            2, 0, // Txn Sequence Number
            0, 0, // Status Code
        ];
        state = state.on_mac_frame(&mut sta, &mut ctx, &auth_resp_success[..], false);
        assert_variant!(state, States::Authenticated(_), "not in auth'ed state");
    }

    #[test]
    fn state_transitions_associng_success() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
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
            3, 3, 3, 3, 3, 3, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Assoc Resp Header:
            0, 0, // Capabilities
            0, 0, // Status Code
            0, 0, // AID
        ];
        state = state.on_mac_frame(&mut sta, &mut ctx, &assoc_resp_success[..], false);
        assert_variant!(state, States::Associated(_), "not in associated state");
    }

    #[test]
    fn state_transitions_associng_failure() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
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
            3, 3, 3, 3, 3, 3, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Assoc Resp Header:
            0, 0, // Capabilities
            2, 0, // Status Code
            0, 0, // AID
        ];
        state = state.on_mac_frame(&mut sta, &mut ctx, &assoc_resp_success[..], false);
        assert_variant!(state, States::Authenticated(_), "not in authenticated state");
    }

    #[test]
    fn state_transitions_associng_timeout() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut state = States::from(statemachine::testing::new_state(Authenticated));

        state = state.associate(&mut sta, &mut ctx);
        let timeout_id = assert_variant!(state, States::Associating(ref state) => {
            state.timeout.clone()
        }, "not in assoc'ing state");
        state = state.on_timed_event(&mut sta, &mut ctx, timeout_id);
        assert_variant!(state, States::Authenticated(_), "not in authenticated state");
    }

    #[test]
    fn state_transitions_associng_deauthing() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
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
            3, 3, 3, 3, 3, 3, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Deauth Header:
            4, 0, // Reason Code
        ];
        state = state.on_mac_frame(&mut sta, &mut ctx, &deauth[..], false);
        assert_variant!(state, States::Joined(_), "not in joined state");
    }

    #[test]
    fn state_transitions_assoced_disassoc() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut state = States::from(statemachine::testing::new_state(Associated {
            aid: 0,
            controlled_port: ControlledPort::Open,
        }));

        // Disassociation: Associating > Authenticated
        #[rustfmt::skip]
        let disassoc = vec![
            // Mgmt Header:
            0b1010_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // Addr1
            3, 3, 3, 3, 3, 3, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Deauth Header:
            4, 0, // Reason Code
        ];
        state = state.on_mac_frame(&mut sta, &mut ctx, &disassoc[..], false);
        assert_variant!(state, States::Authenticated(_), "not in authenticated state");
    }

    #[test]
    fn state_transitions_assoced_deauthing() {
        let mut m = MockObjects::new();
        let mut ctx = m.make_ctx();
        let mut sta = make_client_station();
        let mut state = States::from(statemachine::testing::new_state(Associated {
            aid: 0,
            controlled_port: ControlledPort::Open,
        }));

        // Deauthentication: Associated > Joined
        #[rustfmt::skip]
        let deauth = vec![
            // Mgmt Header:
            0b1100_00_00, 0b00000000, // Frame Control
            0, 0, // Duration
            6, 6, 6, 6, 6, 6, // Addr1
            3, 3, 3, 3, 3, 3, // Addr2
            6, 6, 6, 6, 6, 6, // Addr3
            0x10, 0, // Sequence Control
            // Deauth Header:
            4, 0, // Reason Code
        ];
        state = state.on_mac_frame(&mut sta, &mut ctx, &deauth[..], false);
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
}
