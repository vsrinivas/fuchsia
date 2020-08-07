// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

pub const NODE_SEPARATOR: &'static str = "#/@";
pub const POLICY_STASH_PREFIX: &str = "config";
/// The StashNode abstraction requires that writing to a StashNode is done as a named field,
/// so we will store the network config's data under the POLICY_DATA_KEY.
pub const POLICY_DATA_KEY: &str = "data";
pub const POLICY_STASH_ID: &str = "saved_networks";

/// The data that will be stored between reboots of a device. Used to convert the data between JSON
/// and network config.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct PersistentData {
    pub credential: Credential,
    pub has_ever_connected: bool,
}

/// The network identifier is the SSID and security policy of the network, and it is used to
/// distinguish networks. It mirrors the NetworkIdentifier in fidl_fuchsia_wlan_policy.
#[derive(Clone, Debug, Deserialize, Eq, Hash, PartialEq, Serialize)]
pub struct NetworkIdentifier {
    pub ssid: Vec<u8>,
    pub security_type: SecurityType,
}

/// The security type of a network connection. It mirrors the fidl_fuchsia_wlan_policy SecurityType
#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, PartialEq, Serialize)]
pub enum SecurityType {
    None,
    Wep,
    Wpa,
    Wpa2,
    Wpa3,
}

/// The credential of a network connection. It mirrors the fidl_fuchsia_wlan_policy Credential
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub enum Credential {
    None,
    Password(Vec<u8>),
    Psk(Vec<u8>),
}
