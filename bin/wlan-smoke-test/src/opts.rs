// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use structopt::StructOpt;

#[derive(StructOpt, Clone, Debug)]
/// Simple integration test verifing basic wifi functionality:
/// list interfaces, get status, connect and use the connection.
pub struct Opt {
    /// SSID of the network to use in the test
    #[structopt(name = "target_ssid", raw(required = "true"))]
    pub target_ssid: String,
    /// password for the target network
    #[structopt(short = "p", long = "target_pwd", default_value = "")]
    pub target_pwd: String,
    /// flag indicating wifi should use existing connections if they match the target ssid
    #[structopt(short = "s", long = "stay_connected")]
    pub stay_connected: bool,
}
