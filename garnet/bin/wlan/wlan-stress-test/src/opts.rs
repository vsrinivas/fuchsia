// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use structopt::StructOpt;

#[derive(StructOpt, Clone, Debug)]
/// Stress test verifing wifi stability:
/// repeat scan, connect and disconnect.
/// If the following options are specified: -s -d -c -r 10,
/// the test will run (scan then connect then disconnect) 10 times
/// Note that the ordering of the options on the command line does not
/// impact the order of execution of the API calls.
/// User can specify a wait time in ms between consecutive repetitions
/// using the '-w' command line option.
pub struct Opt {
    /// SSID of the network to use in the test
    #[structopt(name = "target_ssid", default_value = "")]
    pub target_ssid: String,
    /// password for the target network
    #[structopt(short = "p", long = "target_pwd", default_value = "")]
    pub target_pwd: String,
    /// flag indicating whether to stress the scan API
    #[structopt(short = "s", long = "scan")]
    pub scan_test_enabled: bool,
    /// flag indicating whether to stress the connect API
    #[structopt(short = "c", long = "connect")]
    pub connect_test_enabled: bool,
    /// flag indicating whether to stress the disconnect API
    #[structopt(short = "d", long = "disconnect")]
    pub disconnect_test_enabled: bool,
    /// flag indicating number of times to call the API
    #[structopt(short = "r", long = "repetitions", default_value = "1")]
    pub repetitions: u128,
    /// wait time (in millisecs) between iterations
    #[structopt(short = "w", long = "wait_time_ms", default_value = "0")]
    pub wait_time_ms: u64,
}
