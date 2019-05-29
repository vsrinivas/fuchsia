// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{bail, ensure};
use fidl_fuchsia_wlan_mlme as fidl_mlme;
use log::error;
use std::boxed::Box;
use std::collections::HashMap;
use wlan_rsn::{
    key::exchange::Key,
    rsna::{SecAssocStatus, SecAssocUpdate, UpdateSink},
};

use crate::{
    ap::{
        aid::{self, AssociationId},
        authenticator::Authenticator,
        event::{self, ClientEvent, Event},
        Context,
    },
    timer::EventId,
    MacAddr, MlmeRequest,
};

#[derive(Debug)]
pub struct RemoteClient {
    pub addr: MacAddr,
    pub aid: AssociationId,
    pub authenticator: Option<Box<Authenticator>>,
    key_exchange_timeout: Option<EventId>,
}

impl RemoteClient {
    fn new(addr: MacAddr, aid: AssociationId, authenticator: Option<Box<Authenticator>>) -> Self {
        RemoteClient { addr, aid, authenticator, key_exchange_timeout: None }
    }

    pub fn handle_timeout(&mut self, event_id: EventId, event: ClientEvent, ctx: &mut Context) {
        match event {
            ClientEvent::KeyExchangeTimeout { attempt }
                if triggered(&self.key_exchange_timeout, event_id) =>
            {
                if attempt < event::KEY_EXCHANGE_MAX_ATTEMPTS {
                    self.initiate_key_exchange(ctx, attempt + 1);
                } else {
                    cancel(&mut self.key_exchange_timeout);
                    ctx.mlme_sink.send(MlmeRequest::Deauthenticate(
                        fidl_mlme::DeauthenticateRequest {
                            peer_sta_address: self.addr.clone(),
                            reason_code: fidl_mlme::ReasonCode::FourwayHandshakeTimeout,
                        },
                    ));
                }
            }
            _ => (),
        }
    }

    pub fn handle_eapol_ind(
        &mut self,
        ind: fidl_mlme::EapolIndication,
        ctx: &mut Context,
    ) -> Result<(), failure::Error> {
        let authenticator = match self.authenticator.as_mut() {
            Some(authenticator) => authenticator,
            None => bail!("ignoring EapolInd msg; BSS is not protected"),
        };
        let mic_size = authenticator.get_negotiated_rsne().mic_size;
        match eapol::key_frame_from_bytes(&ind.data, mic_size).to_full_result() {
            Ok(key_frame) => {
                let frame = eapol::Frame::Key(key_frame);
                let mut update_sink = UpdateSink::default();
                match authenticator.on_eapol_frame(&mut update_sink, &frame) {
                    Ok(()) => self.process_authenticator_updates(&update_sink, ctx),
                    Err(e) => bail!("failed processing EAPoL key frame: {}", e),
                }
            }
            Err(_) => bail!("error parsing EAPoL key frame"),
        }
        Ok(())
    }

    pub fn initiate_key_exchange(&mut self, ctx: &mut Context, attempt: u32) {
        let event_id = self.schedule_timer(ClientEvent::KeyExchangeTimeout { attempt }, ctx);
        self.key_exchange_timeout.replace(event_id);
        match self.authenticator.as_mut() {
            Some(authenticator) => {
                let mut update_sink = UpdateSink::default();
                match authenticator.initiate(&mut update_sink) {
                    Ok(()) => self.process_authenticator_updates(&update_sink, ctx),
                    Err(e) => error!("error initiating key exchange: {}", e),
                }
            }
            None => error!("authenticator not found for {:?}", self.addr),
        }
    }

    fn schedule_timer(&self, event: ClientEvent, ctx: &mut Context) -> EventId {
        ctx.timer.schedule(Event::Client { addr: self.addr.clone(), event })
    }

    fn process_authenticator_updates(&mut self, update_sink: &UpdateSink, ctx: &mut Context) {
        for update in update_sink {
            match update {
                SecAssocUpdate::TxEapolKeyFrame(frame) => {
                    let mut buf = Vec::with_capacity(frame.len());
                    frame.as_bytes(false, &mut buf);
                    let a_addr = ctx.device_info.addr.clone();
                    ctx.mlme_sink.send(MlmeRequest::Eapol(fidl_mlme::EapolRequest {
                        src_addr: a_addr,
                        dst_addr: self.addr.clone(),
                        data: buf,
                    }));
                }
                SecAssocUpdate::Key(key) => self.send_key(key, ctx),
                SecAssocUpdate::Status(status) => match status {
                    SecAssocStatus::EssSaEstablished => {
                        cancel(&mut self.key_exchange_timeout);
                        ctx.mlme_sink.send(MlmeRequest::SetCtrlPort(
                            fidl_mlme::SetControlledPortRequest {
                                peer_sta_address: self.addr.clone(),
                                state: fidl_mlme::ControlledPortState::Open,
                            },
                        ));
                    }
                    _ => (),
                },
            }
        }
    }

    fn send_key(&mut self, key: &Key, ctx: &mut Context) {
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
}

fn triggered(id: &Option<EventId>, received_id: EventId) -> bool {
    id.map_or(false, |id| id == received_id)
}

fn cancel(event_id: &mut Option<EventId>) {
    let _ = event_id.take();
}

#[derive(Default)]
pub struct Map {
    clients: HashMap<MacAddr, RemoteClient>,
    aid_map: aid::Map,
}

impl Map {
    pub fn add_client(
        &mut self,
        addr: MacAddr,
        authenticator: Option<Box<Authenticator>>,
    ) -> Result<AssociationId, failure::Error> {
        ensure!(self.get_client(&addr).is_none(), "client already exists in map");

        let aid = self.aid_map.assign_aid()?;
        let remote_client = RemoteClient::new(addr, aid, authenticator);
        self.clients.insert(addr, remote_client);
        Ok(aid)
    }

    pub fn get_client(&self, addr: &MacAddr) -> Option<&RemoteClient> {
        self.clients.get(addr)
    }

    pub fn get_mut_client(&mut self, addr: &MacAddr) -> Option<&mut RemoteClient> {
        self.clients.get_mut(addr)
    }

    pub fn remove_client(&mut self, addr: &MacAddr) -> Option<RemoteClient> {
        let remote_client = self.clients.remove(addr);
        remote_client.as_ref().map(|rc| self.aid_map.release_aid(rc.aid));
        remote_client
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        ap::{event::Event, test_utils::MockAuthenticator, TimeStream},
        sink::MlmeSink,
        test_utils, timer, MacAddr, MlmeStream,
    };
    use {
        futures::channel::mpsc,
        std::{
            error::Error,
            sync::{Arc, Mutex},
        },
    };

    const AP_ADDR: MacAddr = [0x11, 0x22, 0x33, 0x44, 0x55, 0x66];
    const CLIENT_ADDR: MacAddr = [0x01, 0x07, 0x77, 0x77, 0x70, 0x10];
    const AID: AssociationId = 1;

    #[test]
    fn test_remote_client_key_handshake() {
        let (mut remote_client, mock_auth) = remote_client();
        let (mut ctx, mut mlme_stream, _) = make_env();

        // Return EAPOL key frame when Authenticator is initiated.
        let update = SecAssocUpdate::TxEapolKeyFrame(test_utils::eapol_key_frame());
        mock_auth.set_initiate_results(vec![update]);
        remote_client.initiate_key_exchange(&mut ctx, 1);

        // Verify that remote client sent out an EAPOL.request for the EAPOL frame returned by the
        // Authenticator
        match mlme_stream.try_next().unwrap().expect("expect mlme message") {
            MlmeRequest::Eapol(eapol_req) => {
                assert_eq!(eapol_req.src_addr, AP_ADDR);
                assert_eq!(eapol_req.dst_addr, CLIENT_ADDR);
                assert_eq!(eapol_req.data, test_utils::eapol_key_frame_bytes());
            }
            _ => panic!("expect eapol response sent to MLME"),
        }

        // On handling EAPOL indication, authenticator derives some keys and signal that it's done
        let ptk_update = SecAssocUpdate::Key(Key::Ptk(test_utils::ptk()));
        let gtk_update = SecAssocUpdate::Key(Key::Gtk(test_utils::gtk()));
        let done_update = SecAssocUpdate::Status(SecAssocStatus::EssSaEstablished);
        mock_auth.set_on_eapol_frame_results(vec![ptk_update, gtk_update, done_update]);
        let eapol_ind = fidl_mlme::EapolIndication {
            src_addr: CLIENT_ADDR,
            dst_addr: AP_ADDR,
            data: test_utils::eapol_key_frame_bytes(),
        };
        remote_client
            .handle_eapol_ind(eapol_ind, &mut ctx)
            .expect("expect handle_eapol_ind to succeed");

        // Verify that remote client then send out request to set those keys
        match mlme_stream.try_next().unwrap().expect("expect mlme message") {
            MlmeRequest::SetKeys(set_keys_req) => {
                assert_eq!(set_keys_req.keylist.len(), 1);
                let k = set_keys_req.keylist.get(0).expect("expect key descriptor");
                assert_eq!(k.key, vec![0xCCu8; test_utils::cipher().tk_bytes().unwrap()]);
                assert_eq!(k.key_id, 0);
                assert_eq!(k.key_type, fidl_mlme::KeyType::Pairwise);
                assert_eq!(k.address, CLIENT_ADDR);
                assert_eq!(k.rsc, 0);
                assert_eq!(k.cipher_suite_oui, [0x00, 0x0F, 0xAC]);
                assert_eq!(k.cipher_suite_type, 4);
            }
            _ => panic!("expect set keys req to MLME"),
        }

        match mlme_stream.try_next().unwrap().expect("expect mlme message") {
            MlmeRequest::SetKeys(set_keys_req) => {
                assert_eq!(set_keys_req.keylist.len(), 1);
                let k = set_keys_req.keylist.get(0).expect("expect key descriptor");
                assert_eq!(k.key, test_utils::gtk_bytes());
                assert_eq!(k.key_id, 2);
                assert_eq!(k.key_type, fidl_mlme::KeyType::Group);
                assert_eq!(k.address, [0xFFu8; 6]);
                assert_eq!(k.rsc, 0);
                assert_eq!(k.cipher_suite_oui, [0x00, 0x0F, 0xAC]);
                assert_eq!(k.cipher_suite_type, 4);
            }
            _ => panic!("expect set keys req to MLME"),
        }

        // Verify that remote client tells MLME to open controlled port
        match mlme_stream.try_next().unwrap().expect("expect mlme message") {
            MlmeRequest::SetCtrlPort(set_ctrl_port_req) => {
                assert_eq!(set_ctrl_port_req.peer_sta_address, CLIENT_ADDR);
                assert_eq!(set_ctrl_port_req.state, fidl_mlme::ControlledPortState::Open);
            }
            _ => panic!("expect set ctrl port req to MLME"),
        }
    }

    #[test]
    fn test_remote_client_key_handshake_timeout() {
        let (mut remote_client, mock_auth) = remote_client();
        let (mut ctx, mut mlme_stream, mut time_stream) = make_env();

        let update = SecAssocUpdate::TxEapolKeyFrame(test_utils::eapol_key_frame());
        mock_auth.set_initiate_results(vec![update]);
        remote_client.initiate_key_exchange(&mut ctx, 1);

        for i in 1..=4 {
            match mlme_stream.try_next().unwrap().expect("expect mlme message") {
                MlmeRequest::Eapol(eapol_req) => {
                    assert_eq!(eapol_req.src_addr, AP_ADDR);
                    assert_eq!(eapol_req.dst_addr, CLIENT_ADDR);
                    assert_eq!(eapol_req.data, test_utils::eapol_key_frame_bytes());
                }
                _ => panic!("expect eapol response sent to MLME - attempt {}", i),
            }

            // Verify timed event was scheduled and use it to trigger timeout
            let (_, timed_event) = time_stream.try_next().unwrap().expect("expect timed event");
            match timed_event.event {
                Event::Client { addr, event } => {
                    assert_eq!(addr, CLIENT_ADDR);
                    let update = SecAssocUpdate::TxEapolKeyFrame(test_utils::eapol_key_frame());
                    mock_auth.set_initiate_results(vec![update]);
                    remote_client.handle_timeout(timed_event.id, event, &mut ctx);
                }
                _ => panic!("expect client timed event"),
            }
        }

        // On the 4th timeout, remote client sends out a deauth request instead
        match mlme_stream.try_next().unwrap().expect("expect mlme message") {
            MlmeRequest::Deauthenticate(deauth_req) => {
                assert_eq!(deauth_req.peer_sta_address, CLIENT_ADDR);
                assert_eq!(deauth_req.reason_code, fidl_mlme::ReasonCode::FourwayHandshakeTimeout);
            }
            _ => panic!("expect deauth req to MLME"),
        }
    }

    #[test]
    fn test_remote_client_ignore_timeout_if_key_handshake_succeeds() {
        let (mut remote_client, mock_auth) = remote_client();
        let (mut ctx, mut mlme_stream, mut time_stream) = make_env();

        let update = SecAssocUpdate::Status(SecAssocStatus::EssSaEstablished);
        mock_auth.set_initiate_results(vec![update]);
        remote_client.initiate_key_exchange(&mut ctx, 1);

        // Clear out the SetCtrlPort request
        match mlme_stream.try_next().unwrap().expect("expect mlme message") {
            MlmeRequest::SetCtrlPort(..) => (), // expected path
            _ => panic!("expect set ctrl port req to MLME"),
        }

        // Verify timed event was scheduled and use it to try to trigger timeout
        let (_, timed_event) = time_stream.try_next().unwrap().expect("expect timed event");
        match timed_event.event {
            Event::Client { event, .. } => {
                let update = SecAssocUpdate::TxEapolKeyFrame(test_utils::eapol_key_frame());
                mock_auth.set_initiate_results(vec![update]);
                remote_client.handle_timeout(timed_event.id, event, &mut ctx);
            }
            _ => panic!("expect client timed event"),
        }

        // Since EssSa was already established, timeout did not trigger.
        match mlme_stream.try_next() {
            Err(e) => assert_eq!(e.description(), "receiver channel is empty"),
            _ => panic!("unexpected event in mlme stream"),
        }
    }

    #[test]
    fn test_remote_client_handle_eapol_ind_invalid() {
        let (mut remote_client, _) = remote_client();
        let (mut ctx, _, _) = make_env();

        let eapol_ind = fidl_mlme::EapolIndication {
            src_addr: CLIENT_ADDR,
            dst_addr: AP_ADDR,
            data: vec![0x66u8, 20],
        };
        match remote_client.handle_eapol_ind(eapol_ind, &mut ctx) {
            Err(e) => assert_eq!(format!("{}", e), "error parsing EAPoL key frame"),
            _ => panic!("expect handle_eapol_ind to fail"),
        }
    }

    #[test]
    fn test_remote_client_map() {
        let mut client_map: Map = Default::default();
        let client_addr1 = addr(1);
        let client_addr2 = addr(2);
        let client_addr3 = addr(3);
        assert_eq!(add_client(&mut client_map, client_addr1).unwrap(), 1);
        assert_eq!(client_map.get_client(&client_addr1).unwrap().aid, 1);
        assert_eq!(add_client(&mut client_map, client_addr2).unwrap(), 2);
        client_map.remove_client(&client_addr1);
        assert_eq!(add_client(&mut client_map, client_addr3).unwrap(), 1);
    }

    #[test]
    fn test_add_client_multiple_times() {
        let mut client_map: Map = Default::default();
        assert!(add_client(&mut client_map, addr(1)).is_ok());
        let result = add_client(&mut client_map, addr(1));
        assert!(result.is_err());
        assert_eq!(format!("{}", result.unwrap_err()), "client already exists in map");
    }

    fn add_client(client_map: &mut Map, addr: MacAddr) -> Result<AssociationId, failure::Error> {
        client_map.add_client(addr, None)
    }

    fn addr(id: u32) -> MacAddr {
        // impl doesn't matter, just need a unique address for each id for our test
        use std::mem;
        let mac_addr: [u8; 4] = unsafe { mem::transmute(id) };
        [mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], 0, 0]
    }

    fn remote_client() -> (RemoteClient, MockAuthenticatorController) {
        let mock_initiate = Arc::new(Mutex::new(UpdateSink::default()));
        let mock_on_eapol_frame = Arc::new(Mutex::new(UpdateSink::default()));
        let authenticator =
            MockAuthenticator::new(mock_initiate.clone(), mock_on_eapol_frame.clone());
        let remote_client = RemoteClient::new(CLIENT_ADDR, AID, Some(Box::new(authenticator)));
        (remote_client, MockAuthenticatorController { mock_initiate, mock_on_eapol_frame })
    }

    fn make_env() -> (Context, MlmeStream, TimeStream) {
        let device_info = test_utils::fake_device_info(AP_ADDR);
        let (mlme_sink, mlme_stream) = mpsc::unbounded();
        let (timer, time_stream) = timer::create_timer();
        let ctx = Context { device_info, mlme_sink: MlmeSink::new(mlme_sink), timer };
        (ctx, mlme_stream, time_stream)
    }

    struct MockAuthenticatorController {
        mock_initiate: Arc<Mutex<UpdateSink>>,
        mock_on_eapol_frame: Arc<Mutex<UpdateSink>>,
    }

    impl MockAuthenticatorController {
        fn set_initiate_results(&self, updates: UpdateSink) {
            *self.mock_initiate.lock().unwrap() = updates;
        }
        fn set_on_eapol_frame_results(&self, updates: UpdateSink) {
            *self.mock_on_eapol_frame.lock().unwrap() = updates;
        }
    }
}
