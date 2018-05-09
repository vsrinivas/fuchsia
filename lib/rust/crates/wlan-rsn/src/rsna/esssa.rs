// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use Error;
use auth;
use eapol;
use failure;
use key::exchange;
use key::exchange::Key;
use key::gtk::Gtk;
use key::ptk::Ptk;
use rsna::SecAssocUpdate;
use rsna::{Role, SecAssocResult};
use rsne::Rsne;
use std::mem;

struct Pmksa {
    method: auth::Method,
    pmk: Option<Vec<u8>>,
}

enum Ptksa {
    Uninitialized(Option<exchange::Config>),
    Initialized(PtksaCfg),
}

impl Ptksa {
    fn initialize(&mut self, pmk: Vec<u8>) -> Result<(), failure::Error> {
        let cfg_option = match self {
            Ptksa::Uninitialized(cfg) => cfg.take(),
            _ => None,
        };
        match cfg_option {
            Some(cfg) => {
                *self = Ptksa::Initialized(PtksaCfg {
                    method: exchange::Method::from_config(cfg, pmk)?,
                    ptk: None,
                });
            }
            _ => (),
        }
        Ok(())
    }

    pub fn by_mut_ref(&mut self) -> &mut Self {
        self
    }
}

struct PtksaCfg {
    method: exchange::Method,
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

        let mut rsna = EssSa {
            role,
            sta_addr,
            peer_addr,
            peer_rsne,
            sta_rsne,
            pmksa: Pmksa {
                method: auth_method,
                pmk: None,
            },
            ptksa: Ptksa::Uninitialized(Some(ptk_exch_cfg)),
        };
        rsna.init_pmksa()?;
        Ok(rsna)
    }

    fn on_key_confirmed(&mut self, key: Key) -> Result<(), failure::Error> {
        match key {
            Key::Pmk(pmk) => {
                self.pmksa.pmk = Some(pmk);
                self.init_ptksa()
            }
            Key::Ptk(ptk) => {
                if let Ptksa::Initialized(ptksa) = self.ptksa.by_mut_ref() {
                    ptksa.ptk = Some(ptk);
                }
                // TODO(hahnr): Received new PTK. Invalidate GTKSA if it was already established.
                Ok(())
            }
            Key::Gtk(_gtk) => {
                // TODO(hahnr): Update GTKSA
                // Once both, PTKSA and GTKSA were established, install keys.
                Ok(())
            }
            _ => Ok(()),
        }
    }

    fn init_pmksa(&mut self) -> Result<(), failure::Error> {
        // PSK allows deriving the PMK without exchanging frames.
        let pmk = match self.pmksa.method.by_ref() {
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

    fn init_ptksa(&mut self) -> Result<(), failure::Error> {
        match self.pmksa.pmk.as_ref() {
            None => Err(Error::PmksaNotEstablished.into()),
            Some(pmk) => self.ptksa.initialize(pmk.to_vec()),
        }
    }

    pub fn on_eapol_frame(&mut self, frame: &eapol::Frame) -> SecAssocResult {
        // Only processes EAPOL Key frames. Drop all other frames silently.
        let updates = match frame {
            &eapol::Frame::Key(ref key_frame) => self.on_eapol_key_frame(&key_frame),
            _ => Ok(vec![]),
        }?;

        // Track keys to correctly update corresponding security associations.
        for update in &updates {
            if let SecAssocUpdate::Key(key) = update {
                self.on_key_confirmed(key.clone());
            }
        }

        Ok(updates)
    }

    fn on_eapol_key_frame(&mut self, frame: &eapol::KeyFrame) -> SecAssocResult {
        // PMKSA must be established before any other security association can be established.
        match self.pmksa.pmk {
            None => self.pmksa.method.on_eapol_key_frame(frame),
            Some(_) => match self.ptksa.by_mut_ref() {
                Ptksa::Uninitialized(_) => Ok(vec![]),
                Ptksa::Initialized(ptksa) => ptksa.method.on_eapol_key_frame(frame),
            },
        }
    }
}
