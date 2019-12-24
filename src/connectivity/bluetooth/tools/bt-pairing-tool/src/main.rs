// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_bluetooth_control::{ControlMarker, PairingDelegateMarker},
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon as zx,
};

mod pairing;

const USAGE: &str = "usage: bt-pairing-tool";

fn main_res() -> Result<(), Error> {
    let mut exec = fasync::Executor::new().context("Error creating event loop")?;

    let bt_svc = connect_to_service::<ControlMarker>()
        .context("Failed to connect to bluetooth control interface")?;

    // Setup pairing delegate
    let (delegate_local, delegate_remote) = zx::Channel::create()?;
    let delegate_local = fasync::Channel::from_channel(delegate_local)?;
    let delegate_ptr = fidl::endpoints::ClientEnd::<PairingDelegateMarker>::new(delegate_remote);
    let pairing_delegate_server = pairing::pairing_delegate(delegate_local);
    let pair_set = bt_svc.set_pairing_delegate(Some(delegate_ptr));

    if !exec.run_singlethreaded(pair_set)? {
        return Err(format_err!(
            "Failed to take ownership of Bluetooth Pairing. Another process is likely already managing this."
        ));
    };

    println!("Now accepting pairing requests.");
    exec.run_singlethreaded(pairing_delegate_server)
        .map_err(|_| format_err!("Failed to run pairing server"))
}

fn main() {
    // Print help and exit.
    if let Some(arg) = std::env::args().skip(1).next() {
        if !(arg == "-h" || arg == "--help") {
            eprintln!("Invalid command: {}", arg);
        }
        eprintln!("{}", USAGE);
        return;
    }
    // Run tool.
    if let Err(e) = main_res() {
        eprintln!("{}", e);
    }
}
