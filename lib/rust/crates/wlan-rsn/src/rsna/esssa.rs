// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use auth;
use eapol;
use failure;
use key::exchange;
use rsna::{Role, SecAssocResult};
use rsne::Rsne;

// IEEE Std 802.11-2016, 12.6.1.3.2
pub struct EssSa {
    // Configuration.
    role: Role,
    sta_addr: [u8; 6],
    sta_rsne: Rsne,
    peer_addr: [u8; 6],
    peer_rsne: Rsne,
    auth_method: auth::Method,
    key_exchange: exchange::Method,

    // Security associations.
    pmksa: Option<Vec<u8>>,
    ptksa: Option<Vec<u8>>,
    gtksa: Option<Vec<u8>>,
    igtksa: Option<Vec<u8>>,
}

impl EssSa {
    pub fn new(
        role: Role, auth_cfg: auth::Config, exchange_cfg: exchange::Config, sta_addr: [u8; 6],
        sta_rsne: Rsne, peer_addr: [u8; 6], peer_rsne: Rsne,
    ) -> Result<EssSa, failure::Error> {
        let key_exchange = exchange::Method::from_config(exchange_cfg)?;
        let auth_method = auth::Method::from_config(auth_cfg)?;

        // Some AKMs allow pre-computing the PMK such as PSK.
        let pmksa = match &auth_method {
            &auth::Method::Psk(ref psk) => Some(psk.compute()),
            _ => None,
        };

        let rsna = EssSa {
            role,
            sta_addr,
            peer_addr,
            peer_rsne,
            sta_rsne,
            key_exchange,
            auth_method,
            pmksa,
            ptksa: None,
            gtksa: None,
            igtksa: None,
        };

        // Initiate security association if STA is Authenticator.
        if let Role::Authenticator = rsna.role {
            rsna.initiate_pmksa()?;
        }
        Ok(rsna)
    }

    fn initiate_pmksa(&self) -> Result<(), failure::Error> {
        match self.pmksa {
            // If PSK was used and PMKSA was already established, initiate PTKSA.
            Some(_) => self.initiate_ptksa(),
            // Initiate authentication methods. Only PSK is used so far and does not need
            // initiation.
            _ => Ok(()),
        }
    }

    fn initiate_ptksa(&self) -> Result<(), failure::Error> {
        match &self.key_exchange {
            &exchange::Method::FourWayHandshake(ref hs) => hs.initiate(),
            _ => Ok(()),
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
        match self.pmksa {
            Some(_) => self.key_exchange.on_eapol_key_frame(frame),
            None => self.auth_method.on_eapol_key_frame(frame),
        }
    }
}
