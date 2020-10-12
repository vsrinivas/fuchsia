// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod state;

use state::*;

use {
    crate::{
        ap::{
            aid,
            event::{ClientEvent, Event},
            Context, MacAddr, MlmeRequest, RsnCfg,
        },
        timer::EventId,
    },
    fidl_fuchsia_wlan_mlme as fidl_mlme, fuchsia_zircon as zx,
    log::error,
    wlan_common::{
        ie::SupportedRate,
        mac::{Aid, CapabilityInfo},
    },
    wlan_rsn::key::{exchange::Key, Tk},
};

pub struct RemoteClient {
    pub addr: MacAddr,
    state: Option<States>,
}

impl RemoteClient {
    pub fn new(addr: MacAddr) -> Self {
        Self { addr, state: Some(States::new_initial()) }
    }

    pub fn aid(&self) -> Option<Aid> {
        // Safe: |state| is never None.
        self.state.as_ref().unwrap().aid()
    }

    pub fn authenticated(&self) -> bool {
        // Safe: |state| is never None.
        self.state.as_ref().unwrap().authenticated()
    }

    pub fn associated(&self) -> bool {
        self.aid().is_some()
    }

    pub fn handle_auth_ind(
        &mut self,
        ctx: &mut Context,
        auth_type: fidl_mlme::AuthenticationTypes,
    ) {
        // Safe: |state| is never None and always replaced with Some(..).
        self.state = Some(self.state.take().unwrap().handle_auth_ind(self, ctx, auth_type));
    }

    pub fn handle_assoc_ind(
        &mut self,
        ctx: &mut Context,
        aid_map: &mut aid::Map,
        ap_capabilities: CapabilityInfo,
        client_capabilities: u16,
        ap_rates: &[SupportedRate],
        client_rates: &[u8],
        rsn_cfg: &Option<RsnCfg>,
        s_rsne: Option<Vec<u8>>,
    ) {
        // Safe: |state| is never None and always replaced with Some(..).
        self.state = Some(self.state.take().unwrap().handle_assoc_ind(
            self,
            ctx,
            aid_map,
            ap_capabilities,
            client_capabilities,
            ap_rates,
            client_rates,
            rsn_cfg,
            s_rsne,
        ));
    }

    pub fn handle_disassoc_ind(&mut self, ctx: &mut Context, aid_map: &mut aid::Map) {
        // Safe: |state| is never None and always replaced with Some(..).
        self.state = Some(self.state.take().unwrap().handle_disassoc_ind(self, ctx, aid_map));
    }

    pub fn handle_eapol_ind(&mut self, ctx: &mut Context, data: &[u8]) {
        // Safe: |state| is never None and always replaced with Some(..).
        self.state = Some(self.state.take().unwrap().handle_eapol_ind(self, ctx, data));
    }

    pub fn handle_timeout(&mut self, ctx: &mut Context, event_id: EventId, event: ClientEvent) {
        // Safe: |state| is never None and always replaced with Some(..).
        self.state = Some(self.state.take().unwrap().handle_timeout(self, ctx, event_id, event))
    }

    /// Sends MLME-AUTHENTICATE.response (IEEE Std 802.11-2016, 6.3.5.5) to the MLME.
    pub fn send_authenticate_resp(
        &mut self,
        ctx: &mut Context,
        result_code: fidl_mlme::AuthenticateResultCodes,
    ) {
        ctx.mlme_sink.send(MlmeRequest::AuthResponse(fidl_mlme::AuthenticateResponse {
            peer_sta_address: self.addr.clone(),
            result_code,
        }))
    }

    /// Sends MLME-DEAUTHENTICATE.request (IEEE Std 802.11-2016, 6.3.6.2) to the MLME.
    pub fn send_deauthenticate_req(
        &mut self,
        ctx: &mut Context,
        reason_code: fidl_mlme::ReasonCode,
    ) {
        ctx.mlme_sink.send(MlmeRequest::Deauthenticate(fidl_mlme::DeauthenticateRequest {
            peer_sta_address: self.addr.clone(),
            reason_code,
        }))
    }

    /// Sends MLME-ASSOCIATE.response (IEEE Std 802.11-2016, 6.3.7.5) to the MLME.
    pub fn send_associate_resp(
        &mut self,
        ctx: &mut Context,
        result_code: fidl_mlme::AssociateResultCodes,
        aid: Aid,
        capabilities: CapabilityInfo,
        rates: Vec<u8>,
    ) {
        ctx.mlme_sink.send(MlmeRequest::AssocResponse(fidl_mlme::AssociateResponse {
            peer_sta_address: self.addr.clone(),
            result_code,
            association_id: aid,
            cap: capabilities.0,
            rates,
        }))
    }

    /// Sends MLME-EAPOL.request (IEEE Std 802.11-2016, 6.3.22.1) to the MLME.
    pub fn send_eapol_req(&mut self, ctx: &mut Context, frame: eapol::KeyFrameBuf) {
        ctx.mlme_sink.send(MlmeRequest::Eapol(fidl_mlme::EapolRequest {
            src_addr: ctx.device_info.mac_addr.clone(),
            dst_addr: self.addr.clone(),
            data: frame.into(),
        }));
    }

    /// Sends SET_CONTROLLED_PORT.request (fuchsia.wlan.mlme.SetControlledPortRequest) to the MLME.
    pub fn send_set_controlled_port_req(
        &mut self,
        ctx: &mut Context,
        port_state: fidl_mlme::ControlledPortState,
    ) {
        ctx.mlme_sink.send(MlmeRequest::SetCtrlPort(fidl_mlme::SetControlledPortRequest {
            peer_sta_address: self.addr.clone(),
            state: port_state,
        }));
    }

    pub fn send_key(&mut self, ctx: &mut Context, key: &Key) {
        let set_key_descriptor = match key {
            Key::Ptk(ptk) => fidl_mlme::SetKeyDescriptor {
                key: ptk.tk().to_vec(),
                key_id: 0,
                key_type: fidl_mlme::KeyType::Pairwise,
                address: self.addr.clone(),
                rsc: 0,
                cipher_suite_oui: eapol::to_array(&ptk.cipher.oui[..]),
                cipher_suite_type: ptk.cipher.suite_type,
            },
            Key::Gtk(gtk) => fidl_mlme::SetKeyDescriptor {
                key: gtk.tk().to_vec(),
                key_id: gtk.key_id() as u16,
                key_type: fidl_mlme::KeyType::Group,
                address: [0xFFu8; 6],
                rsc: gtk.rsc,
                cipher_suite_oui: eapol::to_array(&gtk.cipher.oui[..]),
                cipher_suite_type: gtk.cipher.suite_type,
            },
            _ => {
                error!("unsupported key type in UpdateSink");
                return;
            }
        };
        ctx.mlme_sink.send(MlmeRequest::SetKeys(fidl_mlme::SetKeysRequest {
            keylist: vec![set_key_descriptor],
        }));
    }

    pub fn schedule_at(
        &mut self,
        ctx: &mut Context,
        deadline: zx::Time,
        event: ClientEvent,
    ) -> EventId {
        ctx.timer.schedule_at(deadline, Event::Client { addr: self.addr.clone(), event })
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{ap::TimeStream, sink::MlmeSink, test_utils, timer, MlmeStream},
        futures::channel::mpsc,
        wlan_common::assert_variant,
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
    fn aid_when_not_associated() {
        let r_sta = make_remote_client();
        assert_eq!(r_sta.aid(), None);
    }

    #[test]
    fn authenticated_when_not_authenticated() {
        let r_sta = make_remote_client();
        assert_eq!(r_sta.authenticated(), false);
    }

    #[test]
    fn authenticated_when_authenticated() {
        let mut r_sta = make_remote_client();
        let (mut ctx, _, _) = make_env();
        r_sta.handle_auth_ind(&mut ctx, fidl_mlme::AuthenticationTypes::OpenSystem);
        assert_eq!(r_sta.authenticated(), true);
    }

    #[test]
    fn authenticated_when_associated() {
        let mut r_sta = make_remote_client();
        let (mut ctx, _, _) = make_env();
        r_sta.handle_auth_ind(&mut ctx, fidl_mlme::AuthenticationTypes::OpenSystem);
        let mut aid_map = aid::Map::default();
        r_sta.handle_assoc_ind(
            &mut ctx,
            &mut aid_map,
            CapabilityInfo(0).with_short_preamble(true),
            CapabilityInfo(0).with_short_preamble(true).raw(),
            &[SupportedRate(0b11111000)][..],
            &[0b11111000][..],
            &None,
            None,
        );
        assert_eq!(r_sta.authenticated(), true);
    }

    #[test]
    fn aid_when_associated() {
        let mut r_sta = make_remote_client();
        let (mut ctx, _, _) = make_env();
        r_sta.handle_auth_ind(&mut ctx, fidl_mlme::AuthenticationTypes::OpenSystem);
        let mut aid_map = aid::Map::default();
        r_sta.handle_assoc_ind(
            &mut ctx,
            &mut aid_map,
            CapabilityInfo(0).with_short_preamble(true),
            CapabilityInfo(0).with_short_preamble(true).raw(),
            &[SupportedRate(0b11111000)][..],
            &[0b11111000][..],
            &None,
            None,
        );
        assert_eq!(r_sta.aid(), Some(1));
    }

    #[test]
    fn aid_after_disassociation() {
        let mut r_sta = make_remote_client();
        let (mut ctx, _, _) = make_env();
        r_sta.handle_auth_ind(&mut ctx, fidl_mlme::AuthenticationTypes::OpenSystem);
        assert_eq!(r_sta.authenticated(), true);
        let mut aid_map = aid::Map::default();
        r_sta.handle_assoc_ind(
            &mut ctx,
            &mut aid_map,
            CapabilityInfo(0).with_short_preamble(true),
            CapabilityInfo(0).with_short_preamble(true).raw(),
            &[SupportedRate(0b11111000)][..],
            &[0b11111000][..],
            &None,
            None,
        );
        assert_variant!(r_sta.aid(), Some(_));
        r_sta.handle_disassoc_ind(&mut ctx, &mut aid_map);
        assert_eq!(r_sta.aid(), None);
    }

    #[test]
    fn disassociate_does_nothing_when_not_associated() {
        let mut r_sta = make_remote_client();
        let (mut ctx, _, _) = make_env();
        let mut aid_map = aid::Map::default();
        r_sta.handle_disassoc_ind(&mut ctx, &mut aid_map);
    }

    #[test]
    fn send_authenticate_resp() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();
        r_sta.send_authenticate_resp(
            &mut ctx,
            fidl_mlme::AuthenticateResultCodes::AntiCloggingTokenRequired,
        );
        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::AuthResponse(fidl_mlme::AuthenticateResponse {
            peer_sta_address,
            result_code,
        }) => {
            assert_eq!(peer_sta_address, CLIENT_ADDR);
            assert_eq!(result_code, fidl_mlme::AuthenticateResultCodes::AntiCloggingTokenRequired);
        });
    }

    #[test]
    fn association_times_out() {
        let mut r_sta = make_remote_client();
        let (mut ctx, _, _) = make_env();
        r_sta.handle_auth_ind(&mut ctx, fidl_mlme::AuthenticationTypes::OpenSystem);
        assert_eq!(r_sta.authenticated(), true);
        // TODO(tonyy): This is kind of fragile: EventId should be opaque, but we're just guessing
        // what it should be here since we can't see into the state machine's EventId.
        r_sta.handle_timeout(&mut ctx, 0, ClientEvent::AssociationTimeout);
        assert_eq!(r_sta.authenticated(), false);
    }

    #[test]
    fn send_associate_resp() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();
        r_sta.send_associate_resp(
            &mut ctx,
            fidl_mlme::AssociateResultCodes::RefusedApOutOfMemory,
            1,
            CapabilityInfo(0).with_short_preamble(true),
            vec![1, 2, 3],
        );
        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::AssocResponse(fidl_mlme::AssociateResponse {
            peer_sta_address,
            result_code,
            association_id,
            cap,
            rates,
        }) => {
            assert_eq!(peer_sta_address, CLIENT_ADDR);
            assert_eq!(result_code, fidl_mlme::AssociateResultCodes::RefusedApOutOfMemory);
            assert_eq!(association_id, 1);
            assert_eq!(cap, CapabilityInfo(0).with_short_preamble(true).raw());
            assert_eq!(rates, vec![1, 2, 3]);
        });
    }

    #[test]
    fn send_deauthenticate_req() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();
        r_sta.send_deauthenticate_req(&mut ctx, fidl_mlme::ReasonCode::NoMoreStas);
        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::Deauthenticate(fidl_mlme::DeauthenticateRequest {
            peer_sta_address,
            reason_code,
        }) => {
            assert_eq!(peer_sta_address, CLIENT_ADDR);
            assert_eq!(reason_code, fidl_mlme::ReasonCode::NoMoreStas);
        });
    }

    #[test]
    fn send_eapol_req() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();
        r_sta.send_eapol_req(&mut ctx, test_utils::eapol_key_frame());
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
    fn send_key_ptk() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();
        r_sta.send_key(&mut ctx, &Key::Ptk(test_utils::ptk()));
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
    fn send_key_gtk() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();
        r_sta.send_key(&mut ctx, &Key::Gtk(test_utils::gtk()));
        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::SetKeys(fidl_mlme::SetKeysRequest { keylist }) => {
            assert_eq!(keylist.len(), 1);
            let k = keylist.get(0).expect("expect key descriptor");
            assert_eq!(k.key, test_utils::gtk_bytes());
            assert_eq!(k.key_id, 2);
            assert_eq!(k.key_type, fidl_mlme::KeyType::Group);
            assert_eq!(k.address, [0xFFu8; 6]);
            assert_eq!(k.rsc, 0);
            assert_eq!(k.cipher_suite_oui, [0x00, 0x0F, 0xAC]);
            assert_eq!(k.cipher_suite_type, 4);
        });
    }

    #[test]
    fn send_set_controlled_port_req() {
        let mut r_sta = make_remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();
        r_sta.send_set_controlled_port_req(&mut ctx, fidl_mlme::ControlledPortState::Open);
        let mlme_event = mlme_stream.try_next().unwrap().expect("expected mlme event");
        assert_variant!(mlme_event, MlmeRequest::SetCtrlPort(fidl_mlme::SetControlledPortRequest {
            peer_sta_address,
            state,
        }) => {
            assert_eq!(peer_sta_address, CLIENT_ADDR);
            assert_eq!(state, fidl_mlme::ControlledPortState::Open);
        });
    }

    #[test]
    fn schedule_at() {
        let mut r_sta = make_remote_client();
        let (mut ctx, _, mut time_stream) = make_env();
        let timeout_event_id = r_sta.schedule_at(
            &mut ctx,
            zx::Time::after(zx::Duration::from_seconds(2)),
            ClientEvent::AssociationTimeout,
        );
        let (_, timed_event) = time_stream.try_next().unwrap().expect("expected timed event");
        assert_eq!(timed_event.id, timeout_event_id);
        assert_variant!(timed_event.event, Event::Client { addr, event } => {
            assert_eq!(addr, CLIENT_ADDR);
            assert_variant!(event, ClientEvent::AssociationTimeout);
        });
    }
}
