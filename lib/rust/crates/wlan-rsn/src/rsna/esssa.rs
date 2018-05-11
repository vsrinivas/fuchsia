// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use auth;
use eapol;
use failure;
use key::exchange;
use key::exchange::Key;
use key::gtk::Gtk;
use key::ptk::Ptk;
use rsna::{Role, SecAssocResult};
use rsne::Rsne;
use Error;

struct Pmksa {
    auth_method: auth::Method,
    pmk: Option<Vec<u8>>,
}

struct Ptksa {
    exchange_method: exchange::Method,
    ptk: Option<Ptk>,
}

// IEEE Std 802.11-2016, 12.6.1.3.2
pub struct EssSa {
    // Configuration.
    role: Role,
    sta_addr: [u8; 6],
    sta_rsne: Rsne,
    peer_addr: [u8; 6],
    peer_rsne: Rsne,

    // Security associations.
    pmksa: Pmksa,
    ptksa: Ptksa,
    // TODO(hahnr): Add GTK and optional IGTK support.
}

impl EssSa {
    pub fn new(
        role: Role, auth_cfg: auth::Config, ptk_exch_cfg: exchange::Config, sta_addr: [u8; 6],
        sta_rsne: Rsne, peer_addr: [u8; 6], peer_rsne: Rsne,
    ) -> Result<EssSa, failure::Error> {
        let auth_method = auth::Method::from_config(auth_cfg)?;
        let ptk_exchange = exchange::Method::from_config(ptk_exch_cfg)?;

        let mut rsna = EssSa {
            role,
            sta_addr,
            peer_addr,
            peer_rsne,
            sta_rsne,
            pmksa: Pmksa {
                auth_method,
                pmk: None,
            },
            ptksa: Ptksa {
                exchange_method: ptk_exchange,
                ptk: None,
            },
        };
        rsna.initiate_pmksa()?;
        Ok(rsna)
    }

    fn on_key_confirmed(&mut self, key: Key) -> Result<(), failure::Error> {
        match key {
            Key::Pmk(pmk) => {
                self.pmksa.pmk = Some(pmk);
                self.initiate_ptksa()
            }
            _ => Ok(()),
        }
    }

    fn initiate_pmksa(&mut self) -> Result<(), failure::Error> {
        // PSK allows deriving the PMK without exchanging frames.
        let pmk = match self.pmksa.auth_method.by_ref() {
            auth::Method::Psk(psk) => Some(psk.compute()),
            _ => None,
        };
        if let Some(pmk_data) = pmk {
            self.on_key_confirmed(Key::Pmk(pmk_data))?;
        }

        // TODO(hahnr): Support 802.1X authentication if STA is Authenticator and authentication
        // method is not PSK.

        Ok(())
    }

    fn initiate_ptksa(&mut self) -> Result<(), failure::Error> {
        match self.pmksa.pmk.as_ref() {
            None => Err(Error::PmksaNotEstablished.into()),
            Some(pmk) => match self.ptksa.exchange_method.by_mut_ref() {
                exchange::Method::FourWayHandshake(hs) => hs.initiate(pmk.to_vec()),
                _ => Ok(()),
            },
        }
    }

    pub fn on_eapol_frame(&self, frame: &eapol::Frame) -> SecAssocResult {
        // Only processes EAPOL Key frames. Drop all other frames silently.
        match frame {
            &eapol::Frame::Key(ref key_frame) => self.on_eapol_key_frame(&key_frame),
            _ => Ok(vec![]),
        }
    }

    fn on_eapol_key_frame(&self, frame: &eapol::KeyFrame) -> SecAssocResult {
        // PMKSA must be established before any other security association can be established.
        match self.pmksa.pmk {
            None => self.pmksa.auth_method.on_eapol_key_frame(frame),
            Some(_) => self.ptksa.exchange_method.on_eapol_key_frame(frame),
        }
    }
}
