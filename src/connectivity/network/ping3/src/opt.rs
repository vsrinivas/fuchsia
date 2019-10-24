// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use structopt::StructOpt;

#[derive(StructOpt, Debug)]
#[structopt(name = "ping3", version = "0.1.0")]
/// Sends ICMP echo requests to a host and displays relies.
///
/// If ping3 does not receive any reply packets at all, it will exit with code 1. If `count` and
/// `deadline` are both specified, and fewer than `count` replies are received by the time the
/// `deadline` has expired, it will also exit with code 1. On other errors it exits with code 2.
/// Otherwise it exits with code 0.
///
/// In other words, exit code 0 implies the host is alive. Code 1 implies the host is dead. Code 2
/// shows the state of the host cannot be determined.
pub struct Opt {
    /// Source IP address of the ICMP echo requests.
    #[structopt(short = "l", long = "local")]
    pub local_addr: Option<String>,

    /// Destination IP address to send ICMP echo requests to.
    #[structopt(name = "remote")]
    pub remote_addr: String,

    /// Specifies the number of data bytes to be sent. The final size of the ICMP packet will be
    /// this value plus 8 bytes for the ICMP header.
    #[structopt(short = "s", long = "size", default_value = "56")]
    pub packet_size: u16,

    /// Milliseconds to wait between sending ICMP echo requests.
    #[structopt(short = "i", long = "interval", default_value = "1000")]
    pub interval: i64,

    /// Number of ICMP echo requests to send before stopping.
    #[structopt(short = "c", long = "count")]
    pub count: Option<u64>,

    /// A timeout, in seconds, before exiting regardless of how many ICMP echo requests have been
    /// sent or how many ICMP echo replies have been received.
    #[structopt(short = "w", long = "deadline")]
    pub deadline: Option<i64>,

    /// Enables detailed logging and tracing
    #[structopt(short = "v", long = "verbose")]
    pub verbose: bool,
}
