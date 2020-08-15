// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod psk;

use crate::{
    key::exchange::Key,
    rsna::{AuthStatus, Dot11VerifiedKeyFrame, SecAssocUpdate, UpdateSink},
};

use anyhow::{self, bail};
use fidl_fuchsia_wlan_mlme::SaeFrame;
use log::warn;
use wlan_common::{
    ie::rsn::akm::{self, Akm},
    mac::MacAddr,
};
use wlan_sae as sae;
use zerocopy::ByteSlice;

pub enum Method {
    Psk(psk::Psk),
    Sae { peer: MacAddr, pmk: Option<sae::Key>, handshake: Box<dyn sae::SaeHandshake> },
}

impl std::fmt::Debug for Method {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::result::Result<(), std::fmt::Error> {
        match self {
            Self::Psk(psk) => write!(f, "Method::Psk({:?})", psk),
            Self::Sae { peer, pmk, .. } => write!(
                f,
                "Method::Sae {{ peer: {:?}, pmk: {}, .. }}",
                peer,
                match pmk {
                    Some(_) => "Some(_)",
                    None => "None",
                }
            ),
        }
    }
}

impl Method {
    pub fn from_config(cfg: Config) -> Result<Method, anyhow::Error> {
        match cfg {
            Config::ComputedPsk(psk) => Ok(Method::Psk(psk)),
            Config::Sae { password, mac, peer_mac } => {
                let handshake = sae::new_sae_handshake(
                    sae::DEFAULT_GROUP_ID,
                    Akm::new_dot11(akm::SAE),
                    password,
                    mac,
                    peer_mac.clone(),
                )?;
                Ok(Method::Sae { peer: peer_mac, pmk: None, handshake })
            }
        }
    }

    // Unused as only PSK is supported so far.
    pub fn on_eapol_key_frame<B: ByteSlice>(
        &self,
        _update_sink: &mut UpdateSink,
        _frame: Dot11VerifiedKeyFrame<B>,
    ) -> Result<(), anyhow::Error> {
        Ok(())
    }

    fn process_sae_updates(
        &mut self,
        peer_sta_address: MacAddr,
        assoc_update_sink: &mut UpdateSink,
        sae_update_sink: sae::SaeUpdateSink,
    ) {
        for sae_update in sae_update_sink {
            match sae_update {
                sae::SaeUpdate::SendFrame(frame) => {
                    let sae_frame = SaeFrame {
                        peer_sta_address,
                        result_code: frame.result_code,
                        seq_num: frame.seq,
                        sae_fields: frame.body,
                    };
                    assoc_update_sink.push(SecAssocUpdate::TxSaeFrame(sae_frame));
                }
                sae::SaeUpdate::Success(key) => {
                    match self {
                        Method::Sae { pmk, .. } => {
                            pmk.replace(key.clone());
                        }
                        _ => (),
                    };
                    assoc_update_sink.push(SecAssocUpdate::Key(Key::Pmk(key.pmk)));
                    assoc_update_sink.push(SecAssocUpdate::SaeAuthStatus(AuthStatus::Success));
                }
                sae::SaeUpdate::Reject(reason) => {
                    warn!("SAE handshake rejected: {:?}", reason);
                    assoc_update_sink.push(SecAssocUpdate::SaeAuthStatus(AuthStatus::Rejected));
                }
                // TODO(42140): Schedule timeouts.
                _ => (),
            }
        }
    }

    pub fn on_sae_handshake_ind(
        &mut self,
        assoc_update_sink: &mut UpdateSink,
    ) -> Result<(), anyhow::Error> {
        match self {
            Method::Sae { peer, handshake, .. } => {
                let mut sae_update_sink = sae::SaeUpdateSink::default();
                handshake.initiate_sae(&mut sae_update_sink);
                let peer = peer.clone();
                self.process_sae_updates(peer, assoc_update_sink, sae_update_sink);
                Ok(())
            }
            method => bail!("Unexpected SaeHandshakeInd for auth method {:?}", method),
        }
    }

    pub fn on_sae_frame_rx(
        &mut self,
        assoc_update_sink: &mut UpdateSink,
        frame: SaeFrame,
    ) -> Result<(), anyhow::Error> {
        match self {
            Method::Sae { peer, handshake, .. } => {
                let mut sae_update_sink = sae::SaeUpdateSink::default();
                let frame_rx = sae::AuthFrameRx {
                    seq: frame.seq_num,
                    result_code: frame.result_code,
                    body: &frame.sae_fields[..],
                };
                handshake.handle_frame(&mut sae_update_sink, &frame_rx);
                let peer = peer.clone();
                self.process_sae_updates(peer, assoc_update_sink, sae_update_sink);
                Ok(())
            }
            method => bail!("Unexpected SaeFrameRx for auth method {:?}", method),
        }
    }
}

#[derive(Debug, PartialEq)]
pub enum Config {
    ComputedPsk(psk::Psk),
    Sae { password: Vec<u8>, mac: MacAddr, peer_mac: MacAddr },
}

#[cfg(test)]
mod test {
    use super::*;
    use std::sync::{Arc, Mutex};
    use wlan_common::assert_variant;

    #[test]
    fn psk_rejects_sae() {
        let mut auth = Method::from_config(Config::ComputedPsk(Box::new([0x8; 16])))
            .expect("Failed to construct PSK auth method");
        let mut sink = UpdateSink::default();
        auth.on_sae_handshake_ind(&mut sink).expect_err("PSK auth method accepted SAE ind");
        let frame = SaeFrame {
            peer_sta_address: [0xaa; 6],
            result_code: fidl_fuchsia_wlan_mlme::AuthenticateResultCodes::Success,
            seq_num: 1,
            sae_fields: vec![0u8; 10],
        };
        auth.on_sae_frame_rx(&mut sink, frame).expect_err("PSK auth method accepted SAE frame");
        // No updates should be queued for these invalid ops.
        assert!(sink.is_empty());
    }

    #[derive(Default)]
    struct SaeCounter {
        initiated: bool,
        handled_commits: u32,
        handled_confirms: u32,
        handled_timeouts: u32,
    }

    struct DummySae(Arc<Mutex<SaeCounter>>);

    // This sends dummy frames as though it is the SAE initiator.
    impl sae::SaeHandshake for DummySae {
        fn initiate_sae(&mut self, sink: &mut sae::SaeUpdateSink) {
            self.0.lock().unwrap().initiated = true;
            sink.push(sae::SaeUpdate::SendFrame(sae::AuthFrameTx {
                seq: 1,
                result_code: fidl_fuchsia_wlan_mlme::AuthenticateResultCodes::Success,
                body: vec![],
            }));
        }
        fn handle_commit(&mut self, _sink: &mut sae::SaeUpdateSink, _commit_msg: &sae::CommitMsg) {
            assert!(self.0.lock().unwrap().initiated);
            self.0.lock().unwrap().handled_commits += 1;
        }
        fn handle_confirm(
            &mut self,
            sink: &mut sae::SaeUpdateSink,
            _confirm_msg: &sae::ConfirmMsg,
        ) {
            assert!(self.0.lock().unwrap().initiated);
            self.0.lock().unwrap().handled_confirms += 1;
            sink.push(sae::SaeUpdate::SendFrame(sae::AuthFrameTx {
                seq: 2,
                result_code: fidl_fuchsia_wlan_mlme::AuthenticateResultCodes::Success,
                body: vec![],
            }));
            sink.push(sae::SaeUpdate::Success(sae::Key { pmk: vec![0xaa], pmkid: vec![0xbb] }))
        }
        fn handle_timeout(&mut self, _sink: &mut sae::SaeUpdateSink, _timeout: sae::Timeout) {
            assert!(self.0.lock().unwrap().initiated);
            self.0.lock().unwrap().handled_timeouts += 1;
        }
    }

    // These are not valid commit and confirm bodies, but are appropriately sized so they will parse.

    const COMMIT: [u8; 98] = [
        0x13, 0x00, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
        0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
        0xaa, 0xaa, 0xaa, 0xaa, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
        0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
        0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
        0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
        0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
    ];
    const CONFIRM: [u8; 34] = [
        0xaa, 0xaa, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
        0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb, 0xbb,
        0xbb, 0xbb, 0xbb, 0xbb,
    ];

    #[test]
    fn sae_executes_handshake() {
        let sae_counter = Arc::new(Mutex::new(SaeCounter::default()));
        let mut auth = Method::Sae {
            peer: [0xaa; 6],
            pmk: None,
            handshake: Box::new(DummySae(sae_counter.clone())),
        };
        let mut sink = UpdateSink::default();

        auth.on_sae_handshake_ind(&mut sink).expect("SAE handshake should accept SAE ind");
        assert!(sae_counter.lock().unwrap().initiated);
        assert_variant!(sink.pop(), Some(SecAssocUpdate::TxSaeFrame(_)));

        let commit_frame = SaeFrame {
            peer_sta_address: [0xaa; 6],
            result_code: fidl_fuchsia_wlan_mlme::AuthenticateResultCodes::Success,
            seq_num: 1,
            sae_fields: COMMIT.to_vec(),
        };
        auth.on_sae_frame_rx(&mut sink, commit_frame).expect("SAE handshake should accept commit");
        assert_eq!(sae_counter.lock().unwrap().handled_commits, 1);
        assert!(sink.is_empty());

        let confirm_frame = SaeFrame {
            peer_sta_address: [0xaa; 6],
            result_code: fidl_fuchsia_wlan_mlme::AuthenticateResultCodes::Success,
            seq_num: 2,
            sae_fields: CONFIRM.to_vec(),
        };
        auth.on_sae_frame_rx(&mut sink, confirm_frame)
            .expect("SAE handshake should accept confirm");
        assert_eq!(sae_counter.lock().unwrap().handled_confirms, 1);
        assert_eq!(sink.len(), 3);
        assert_variant!(sink.remove(0), SecAssocUpdate::TxSaeFrame(_));
        assert_variant!(sink.remove(0), SecAssocUpdate::Key(_));
        assert_variant!(sink.remove(0), SecAssocUpdate::SaeAuthStatus(AuthStatus::Success));
        match auth {
            Method::Sae { pmk, .. } => assert!(pmk.is_some()),
            _ => unreachable!(),
        };
    }
}
