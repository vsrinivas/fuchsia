// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod handshake;

use self::handshake::{
    fourway::{self, Fourway},
    group_key::{self, GroupKey},
};
use crate::key::gtk::Gtk;
use crate::key::ptk::Ptk;
use crate::rsna::{UpdateSink, VerifiedKeyFrame};
use failure;

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
    pub fn name(&self) -> &'static str {
        match self {
            Key::Pmk(..) => "PMK",
            Key::Ptk(..) => "PTK",
            Key::Gtk(..) => "GTK",
            Key::Igtk(..) => "IGTK",
            Key::MicRx(..) => "MIC_RX",
            Key::MicTx(..) => "MIC_TX",
            Key::Smk(..) => "SMK",
            Key::Stk(..) => "STK",
        }
    }
}

#[derive(Debug, PartialEq)]
pub enum Method {
    FourWayHandshake(Fourway),
    GroupKeyHandshake(GroupKey),
}

impl Method {
    pub fn on_eapol_key_frame(
        &mut self,
        update_sink: &mut UpdateSink,
        key_replay_counter: u64,
        frame: VerifiedKeyFrame,
    ) -> Result<(), failure::Error> {
        match self {
            Method::FourWayHandshake(hs) => {
                hs.on_eapol_key_frame(update_sink, key_replay_counter, frame)
            }
            Method::GroupKeyHandshake(hs) => {
                hs.on_eapol_key_frame(update_sink, key_replay_counter, frame)
            }
        }
    }
    pub fn initiate(
        &mut self,
        update_sink: &mut UpdateSink,
        key_replay_counter: u64,
    ) -> Result<(), failure::Error> {
        match self {
            Method::FourWayHandshake(hs) => hs.initiate(update_sink, key_replay_counter),
            // Only 4-Way Handshake supports initiation so far.
            _ => Ok(()),
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
