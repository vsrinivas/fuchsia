// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! `sktool` is a basic tool to scan a Fuchsia system for FIDO security keys and perform simple
//! operations on those security keys.
#![deny(missing_docs)]

use failure::Error;
use log::info;

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["auth"]).expect("Can't init logger");
    info!("Starting Security Key tool.");
    println!("Hello security keys are you there?");
    Ok(())
}
