// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use {fuchsia_syslog as syslog, log::*};

mod client;

fn init_syslog() {
    syslog::init_with_tags(&[]).expect("should not fail");
    debug!("Driver did init logger");
}

// TBD whether to launch driver from test or from main. See client.rs.
fn main() {
    init_syslog();
    println!("Hello, world!");
    info!("Hi World!")
}
