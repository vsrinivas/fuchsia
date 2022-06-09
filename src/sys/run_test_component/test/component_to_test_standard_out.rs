// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

fn main() {
    fuchsia_syslog::init().expect("initializing logging");
    println!("writing to stdout");
    // check that we can handle zeros dumped to stdout.
    println!("writing zeros: \0\n\0, and some bytes");
    eprintln!("writing to stderr");
    log::info!("my info message.");
    println!("writing second message to stdout");
    log::warn!("my warn message.");
    println!("This component will exit now.");
}
