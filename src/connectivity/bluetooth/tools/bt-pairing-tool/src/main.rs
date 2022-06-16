// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    argh::FromArgs,
    fidl_fuchsia_bluetooth_sys::{PairingDelegateMarker, PairingMarker},
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::io_capabilities::{InputCapability, OutputCapability},
    fuchsia_component::client::connect_to_protocol,
    futures::channel::mpsc::channel,
    pairing_delegate,
};

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

    let pairing = connect_to_protocol::<PairingMarker>()
        .context("Failed to connect to bluetooth pairing interface")?;

    // Setup pairing delegate
    let (pairing_delegate_client, pairing_delegate_server_stream) =
        fidl::endpoints::create_request_stream::<PairingDelegateMarker>()
            .expect("cannot create request stream");
    let (sig_sender, _sig_receiver) = channel(0);
    let pairing_delegate_server =
        pairing_delegate::handle_requests(pairing_delegate_server_stream, sig_sender);

    let pair_set =
        pairing.set_pairing_delegate(opt.input.into(), opt.output.into(), pairing_delegate_client);

    if let Err(err) = pair_set {
        return Err(format_err!(
            "Failed to take ownership of Bluetooth Pairing. Another process is likely already managing this. {}", err));
    };

    println!("Now accepting pairing requests.");
    exec.run_singlethreaded(pairing_delegate_server)
        .map_err(|e| format_err!("Failed to run pairing server: {:?}", e))
}

fn main() {
    let opt: Opt = argh::from_env();

    // Run tool.
    if let Err(e) = run(opt) {
        eprintln!("{}", e);
    }
}
