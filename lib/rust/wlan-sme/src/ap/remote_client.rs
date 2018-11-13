// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{bail, ensure};
use fidl_fuchsia_wlan_mlme as fidl_mlme;
use log::error;
use std::collections::HashMap;
use wlan_rsn::{
    Authenticator,
    key::exchange::Key,
    rsna::{UpdateSink, SecAssocStatus, SecAssocUpdate},
};

use crate::{
    MacAddr, MlmeRequest,
    ap::{
        Context, Tokens,
        aid::{self, AssociationId},
        event::{self, ClientEvent, Event},
    },
    timer::EventId,
};

#[derive(Debug)]
pub struct RemoteClient {
    pub addr: MacAddr,
    pub aid: AssociationId,
    pub authenticator: Option<Authenticator>,
    key_exchange_timeout: Option<EventId>,
}

impl RemoteClient {
    fn new(addr: MacAddr, aid: AssociationId, authenticator: Option<Authenticator>) -> Self {
        RemoteClient {
            addr,
            aid,
            authenticator,
            key_exchange_timeout: None,
        }
    }

    pub fn handle_timeout<T: Tokens>(&mut self, event_id: EventId, event: ClientEvent,
                                     ctx: &mut Context<T>) {
        match event {
            ClientEvent::KeyExchangeTimeout { attempt }
                if triggered(&self.key_exchange_timeout, event_id) => {

                if attempt < event::KEY_EXCHANGE_MAX_ATTEMPTS {
                    self.initiate_key_exchange(ctx, attempt + 1);
                } else {
                    cancel(&mut self.key_exchange_timeout);
                    ctx.mlme_sink.send(MlmeRequest::Deauthenticate(
                        fidl_mlme::DeauthenticateRequest {
                            peer_sta_address: self.addr.clone(),
                            reason_code: fidl_mlme::ReasonCode::FourwayHandshakeTimeout,
                        }
                    ));
                }
            }
            _ => (),
        }
    }

    pub fn handle_eapol_ind<T: Tokens>(&mut self, ind: fidl_mlme::EapolIndication,
                                       ctx: &mut Context<T>)
        -> Result<(), failure::Error> {

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
            },
            Err(_) => bail!("error parsing EAPoL key frame"),
        }
        Ok(())
    }

    pub fn initiate_key_exchange<T: Tokens>(&mut self, ctx: &mut Context<T>, attempt: u32) {
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

    fn schedule_timer<T: Tokens>(&self, event: ClientEvent, ctx: &mut Context<T>) -> EventId {
        let event = Event::Client { addr: self.addr.clone(), event };
        ctx.timer.schedule(event.timeout_duration().after_now(), event)
    }

    fn process_authenticator_updates<T: Tokens>(&mut self, update_sink: &UpdateSink,
                                                ctx: &mut Context<T>) {
        for update in update_sink {
            match update {
                SecAssocUpdate::TxEapolKeyFrame(frame) => {
                    let mut buf = Vec::with_capacity(frame.len());
                    frame.as_bytes(false, &mut buf);
                    let a_addr = ctx.device_info.addr.clone();
                    ctx.mlme_sink.send(MlmeRequest::Eapol(
                        fidl_mlme::EapolRequest {
                            src_addr: a_addr,
                            dst_addr: self.addr.clone(),
                            data: buf,
                        }
                    ));
                }
                SecAssocUpdate::Key(key) => self.send_key(key, ctx),
                SecAssocUpdate::Status(status) => match status {
                    SecAssocStatus::EssSaEstablished => cancel(&mut self.key_exchange_timeout),
                    _ => (),
                }
            }
        }
    }

    fn send_key<T: Tokens>(&mut self, key: &Key, ctx: &mut Context<T>) {
        let set_key_descriptor = match key {
            Key::Ptk(ptk) => {
                fidl_mlme::SetKeyDescriptor {
                    key: ptk.tk().to_vec(),
                    key_id: 0,
                    key_type: fidl_mlme::KeyType::Pairwise,
                    address: self.addr.clone(),
                    rsc: [0u8; 8],
                    cipher_suite_oui: eapol::to_array(&ptk.cipher.oui[..]),
                    cipher_suite_type: ptk.cipher.suite_type,
                }
            }
            Key::Gtk(gtk) => {
                fidl_mlme::SetKeyDescriptor {
                    key: gtk.tk().to_vec(),
                    key_id: gtk.key_id() as u16,
                    key_type: fidl_mlme::KeyType::Group,
                    address: [0xFFu8; 6],
                    rsc: [0u8; 8],
                    cipher_suite_oui: eapol::to_array(&gtk.cipher.oui[..]),
                    cipher_suite_type: gtk.cipher.suite_type,
                }
            }
            _ => {
                error!("unsupported key type in UpdateSink");
                return;
            }
        };
        ctx.mlme_sink.send(MlmeRequest::SetKeys(
            fidl_mlme::SetKeysRequest {
                keylist: vec![set_key_descriptor]
            }
        ));
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
    pub fn add_client(&mut self, addr: MacAddr, authenticator: Option<Authenticator>)
                      -> Result<AssociationId, failure::Error> {
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

    fn add_client(client_map: &mut Map, addr: MacAddr)
                  -> Result<AssociationId, failure::Error> {
        client_map.add_client(addr, None)
    }

    fn addr(id: u32) -> MacAddr {
        // impl doesn't matter, just need a unique address for each id for our test
        use std::mem;
        let mac_addr: [u8; 4] = unsafe { mem::transmute(id) };
        [mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], 0, 0]
    }
}
