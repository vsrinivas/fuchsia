// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Debug, Clone)]
pub enum NetAddress {
    V4([u8; 4], u32),
    V6([u8; 16], u32),
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct WANConfiguration {
    pub addr: NetAddress,
    pub dynamically_assigned: bool, // Was addr assigned by dhcp?
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct DHCPConfiguration {
    pub dns_server_addr: NetAddress,
    pub netmask: NetAddress,
    pub addr_range: (NetAddress, NetAddress),
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct APConfiguration {
    pub ssid: Vec<u8>,
    pub password: String,
}
