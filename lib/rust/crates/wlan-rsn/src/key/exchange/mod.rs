// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod handshake;

use self::handshake::{fourway::{self, Fourway}, group_key::{self, GroupKey}};
use crate::akm::Akm;
use failure;
use crate::key::gtk::Gtk;
use crate::key::ptk::Ptk;
use crate::rsna::{Role, SecAssocResult, VerifiedKeyFrame};
use crate::rsne::Rsne;

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

#[derive(Debug, PartialEq)]
pub enum Method {
    FourWayHandshake(Fourway),
    GroupKeyHandshake(GroupKey),
}

impl Method {
    pub fn on_eapol_key_frame(&mut self, frame: VerifiedKeyFrame) -> SecAssocResult {
        match self {
            Method::FourWayHandshake(hs) => hs.on_eapol_key_frame(frame),
            Method::GroupKeyHandshake(hs) => hs.on_eapol_key_frame(frame),
        }
    }

    pub fn destroy(self) -> Config {
        match self {
            Method::FourWayHandshake(hs) => hs.destroy(),
            Method::GroupKeyHandshake(hs) => hs.destroy(),
        }
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

    pub fn for_groupkey_handshake(role: Role, akm: Akm) -> Config
    {
        Config::GroupKeyHandshake(group_key::Config{role, akm})
    }
}
