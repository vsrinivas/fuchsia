// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Expectations for Bluetooth Peers (i.e. Remote Devices)

//use fidl_fuchsia_bluetooth_control::{RemoteDevice, TechnologyType};
use fidl_fuchsia_bluetooth_sys::TechnologyType;

use crate::{
    expectation::Predicate,
    over,
    types::{Address, Peer, PeerId},
};

pub fn name(expected_name: &str) -> Predicate<Peer> {
    over!(Peer: name, Predicate::equal(Some(expected_name.to_string())))
}
pub fn identifier(expected_ident: PeerId) -> Predicate<Peer> {
    over!(Peer: id, Predicate::equal(expected_ident))
}
pub fn address(expected_address: Address) -> Predicate<Peer> {
    over!(Peer: address, Predicate::equal(expected_address))
}
pub fn technology(tech: TechnologyType) -> Predicate<Peer> {
    over!(Peer: technology, Predicate::equal(tech))
}
pub fn connected(connected: bool) -> Predicate<Peer> {
    over!(Peer: connected, Predicate::equal(connected))
}
pub fn bonded(bonded: bool) -> Predicate<Peer> {
    over!(Peer: bonded, Predicate::equal(bonded))
}
