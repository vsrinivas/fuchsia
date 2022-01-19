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

use std::fmt;
pub use time::TimeUnit;

#[derive(Copy, Clone)]
pub enum StationMode {
    Client,
    Ap,
    Mesh,
}

impl fmt::Display for StationMode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            StationMode::Client => f.write_str("client"),
            StationMode::Ap => f.write_str("AP"),
            StationMode::Mesh => f.write_str("mesh"),
        }
    }
}
