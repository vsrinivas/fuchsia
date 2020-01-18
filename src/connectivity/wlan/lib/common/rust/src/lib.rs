// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO Follow 2018 idioms
#![allow(elided_lifetimes_in_paths)]

//! Crate wlan-common hosts common libraries
//! to be used for WLAN SME, MLME, and binaries written in Rust.

#![cfg_attr(feature = "benchmark", feature(test))]
pub mod appendable;
pub mod big_endian;
pub mod bss;
pub mod buffer_reader;
pub mod buffer_writer;
pub mod channel;
pub mod data_writer;
#[allow(unused)]
pub mod energy;
pub mod error;
pub mod format;
pub mod ie;
pub mod mac;
pub mod mgmt_writer;
pub mod organization;
pub mod sequence;
#[allow(unused)]
pub mod stats;
pub mod test_utils;
pub mod tim;
pub mod time;
pub mod unaligned_view;
pub mod wmm;

use {
    channel::{Cbw, Phy},
    fidl_fuchsia_wlan_sme as fidl_sme,
};

pub use time::TimeUnit;

#[derive(Clone, Debug, Default, PartialEq)]
pub struct RadioConfig {
    pub phy: Option<Phy>,
    pub cbw: Option<Cbw>,
    pub primary_chan: Option<u8>,
}

impl RadioConfig {
    pub fn new(phy: Phy, cbw: Cbw, primary_chan: u8) -> Self {
        RadioConfig { phy: Some(phy), cbw: Some(cbw), primary_chan: Some(primary_chan) }
    }

    pub fn to_fidl(&self) -> fidl_sme::RadioConfig {
        let (cbw, _) = self.cbw.or(Some(Cbw::Cbw20)).unwrap().to_fidl();
        fidl_sme::RadioConfig {
            override_phy: self.phy.is_some(),
            phy: self.phy.or(Some(Phy::Ht)).unwrap().to_fidl(),
            override_cbw: self.cbw.is_some(),
            cbw,
            override_primary_chan: self.primary_chan.is_some(),
            primary_chan: self.primary_chan.unwrap_or(0),
        }
    }

    pub fn from_fidl(radio_cfg: fidl_sme::RadioConfig) -> Self {
        RadioConfig {
            phy: if radio_cfg.override_phy { Some(Phy::from_fidl(radio_cfg.phy)) } else { None },
            cbw: if radio_cfg.override_cbw { Some(Cbw::from_fidl(radio_cfg.cbw, 0)) } else { None },
            primary_chan: if radio_cfg.override_primary_chan {
                Some(radio_cfg.primary_chan)
            } else {
                None
            },
        }
    }
}
