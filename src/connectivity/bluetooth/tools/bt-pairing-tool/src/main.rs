// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    argh::FromArgs,
    fidl_fuchsia_bluetooth_sys::{AccessMarker, PairingDelegateMarker},
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::io_capabilities::{InputCapability, OutputCapability},
    fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon as zx,
};

mod pairing;

// Defines all the command line arguments accepted by the tool.
#[derive(FromArgs)]
#[argh(description = "CLI pairing delegate")]
struct Opt {
    #[argh(
        option,
        short = 'i',
        default = "InputCapability::None",
        description = "input capability (none, confirmation, keyboard)"
    )]
    input: InputCapability,
    #[argh(
        option,
        short = 'o',
        default = "OutputCapability::None",
        description = "output capability (none, display)"
    )]
    output: OutputCapability,
}

fn run(opt: Opt) -> Result<(), Error> {
    let mut exec = fasync::LocalExecutor::new().context("Error creating event loop")?;

    let access = connect_to_protocol::<AccessMarker>()
        .context("Failed to connect to bluetooth access interface")?;

    // Setup pairing delegate
    let (delegate_local, delegate_remote) = zx::Channel::create()?;
    let delegate_local = fasync::Channel::from_channel(delegate_local)?;
    let pairing_delegate_server = pairing::pairing_delegate(delegate_local);
    let pairing_delegate_client =
        fidl::endpoints::ClientEnd::<PairingDelegateMarker>::new(delegate_remote);

    let pair_set =
        access.set_pairing_delegate(opt.input.into(), opt.output.into(), pairing_delegate_client);

    if let Err(err) = pair_set {
        return Err(format_err!(
            "Failed to take ownership of Bluetooth Pairing. Another process is likely already managing this. {}", err));
    };

    println!("Now accepting pairing requests.");
    exec.run_singlethreaded(pairing_delegate_server)
        .map_err(|_| format_err!("Failed to run pairing server"))
}

fn main() {
    let opt: Opt = argh::from_env();

    // Run tool.
    if let Err(e) = run(opt) {
        eprintln!("{}", e);
    }
}
