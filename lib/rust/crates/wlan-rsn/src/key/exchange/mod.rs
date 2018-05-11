// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod handshake;

use self::handshake::fourway::{self, Fourway};
use Error;
use eapol;
use failure;
use rsna::{Role, SecAssocResult};
use rsne::Rsne;

pub enum Key {
    Invalid,
    Pmk(Vec<u8>),
    Ptk(Vec<u8>),
    Gtk(Vec<u8>),
    Igtk(Vec<u8>),
    MicRx(Vec<u8>),
    MicTx(Vec<u8>),
    Smk(Vec<u8>),
    Stk(Vec<u8>),
}

impl Key {
    pub fn by_ref(&self) -> &Self {
        self
    }
}

pub enum Method {
    FourWayHandshake(Fourway),
    // TODO(hahnr): Add Group Key Handshake support,
}

impl Method {
    pub fn from_config(cfg: Config) -> Result<Method, failure::Error> {
        match cfg {
            Config::FourWayHandshake(c) => Ok(Method::FourWayHandshake(Fourway::new(c)?)),
            _ => Err(Error::UnknownKeyExchange.into()),
        }
    }

    pub fn on_eapol_key_frame(&self, frame: &eapol::KeyFrame) -> SecAssocResult {
        match self {
            &Method::FourWayHandshake(ref hs) => hs.on_eapol_key_frame(frame),
            _ => Ok(vec![]),
        }
    }

    pub fn by_mut_ref(&mut self) -> &mut Self {
        self
    }
}

pub enum Config {
    FourWayHandshake(fourway::Config),
    // TODO(hahnr): Add Group Key Handshake support,
}

impl Config {
    pub fn for_4way_handshake(
        role: Role, pmk: Vec<u8>, sta_addr: [u8; 6], sta_rsne: Rsne, peer_addr: [u8; 6],
        peer_rsne: Rsne,
    ) -> Result<Config, failure::Error> {
        fourway::Config::new(role, pmk, sta_addr, sta_rsne, peer_addr, peer_rsne)
            .map_err(|e| e.into())
            .map(|c| Config::FourWayHandshake(c))
    }

    pub fn by_ref(&self) -> &Self {
        self
    }
}
