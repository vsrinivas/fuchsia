// Copyright 2021 The Fuchsia Authors. All rights reserved.
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
pub mod capabilities;
pub mod channel;
pub mod data_writer;
#[allow(unused)]
pub mod energy;
pub mod error;
pub mod format;
pub mod hasher;
pub mod ie;
pub mod mac;
pub mod mgmt_writer;
pub mod organization;
pub mod scan;
pub mod security;
pub mod sequence;
pub mod sink;
#[allow(unused)]
pub mod stats;
#[cfg(target_os = "fuchsia")]
pub mod test_utils;
pub mod tim;
pub mod time;
#[cfg(target_os = "fuchsia")]
pub mod timer;
pub mod tx_vector;
pub mod unaligned_view;
pub mod wmm;

use {
    channel::{Cbw, Channel},
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_sme as fidl_sme,
};

pub use time::TimeUnit;

#[derive(Clone, Debug, PartialEq)]
pub struct RadioConfig {
    pub phy: fidl_common::WlanPhyType,
    pub channel: Channel,
}

impl From<RadioConfig> for fidl_sme::RadioConfig {
    fn from(radio_cfg: RadioConfig) -> fidl_sme::RadioConfig {
        fidl_sme::RadioConfig { phy: radio_cfg.phy, channel: radio_cfg.channel.into() }
    }
}

impl From<fidl_sme::RadioConfig> for RadioConfig {
    fn from(fidl_radio_cfg: fidl_sme::RadioConfig) -> RadioConfig {
        RadioConfig { phy: fidl_radio_cfg.phy, channel: fidl_radio_cfg.channel.into() }
    }
}

impl RadioConfig {
    pub fn new(phy: fidl_common::WlanPhyType, cbw: Cbw, primary_channel: u8) -> Self {
        RadioConfig { phy, channel: Channel::new(primary_channel, cbw) }
    }
}
