// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod handshake;

use self::handshake::{fourway::{self, Fourway}, group_key::{self, GroupKey}};
use Error;
use eapol;
use failure;
use key::gtk::Gtk;
use key::ptk::Ptk;
use rsna::{Role, SecAssocResult, VerifiedKeyFrame};
use rsne::Rsne;

#[derive(Debug, Clone, PartialEq)]
pub enum Key {
    Pmk(Vec<u8>),
    Ptk(Ptk),
    Gtk(Gtk),
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

#[derive(Debug, PartialEq)]
pub enum Method {
    FourWayHandshake(Fourway),
    GroupKeyHandshake(GroupKey),
}

impl Method {
    pub fn from_config(cfg: Config, key: Vec<u8>) -> Result<Method, failure::Error> {
        match cfg {
            Config::FourWayHandshake(c) => Ok(Method::FourWayHandshake(Fourway::new(c, key)?)),
            Config::GroupKeyHandshake(c) => Ok(Method::GroupKeyHandshake(GroupKey::new(c, key)?)),
        }
    }

    pub fn on_eapol_key_frame(&mut self, frame: VerifiedKeyFrame) -> SecAssocResult {
        match self {
            Method::FourWayHandshake(hs) => hs.on_eapol_key_frame(frame),
            Method::GroupKeyHandshake(hs) => hs.on_eapol_key_frame(frame),
        }
    }

    pub fn by_mut_ref(&mut self) -> &mut Self {
        self
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum Config {
    FourWayHandshake(fourway::Config),
    GroupKeyHandshake(group_key::Config),
}

impl Config {
    pub fn for_4way_handshake(
        role: Role,
        sta_addr: [u8; 6],
        sta_rsne: Rsne,
        peer_addr: [u8; 6],
        peer_rsne: Rsne,
    ) -> Result<Config, failure::Error> {
        fourway::Config::new(role, sta_addr, sta_rsne, peer_addr, peer_rsne)
            .map_err(|e| e.into())
            .map(|c| Config::FourWayHandshake(c))
    }

    pub fn for_groupkey_handshake(role: Role, sta_addr: [u8; 6], peer_addr: [u8; 6])
        -> Result<Config, failure::Error>
    {
        Ok(Config::GroupKeyHandshake(group_key::Config{role, sta_addr, peer_addr}))
    }

    pub fn by_ref(&self) -> &Self {
        self
    }
}
