// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use structopt::StructOpt;

#[derive(StructOpt, Clone, Debug)]
/// Simple integration test verifing basic AP WiFi functionality:
/// list interfaces, get status, start AP on an interface, have clients
//  scan, connect and disconnect from the AP, stop AP. Repeat for
//  all interfaces that support AP role.
pub struct Opt {
    /// SSID of the network to use in the test
    #[structopt(name = "target_ssid", raw(required = "true"))]
    pub target_ssid: String,
    /// password for the target network
    #[structopt(short = "p", long = "target_pwd", default_value = "")]
    pub target_pwd: String,
    #[structopt(short = "c", long = "target_channel", default_value = "6")]
    pub target_channel: u8,
}
