// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl::endpoints::create_proxy;
use fidl_fuchsia_examples::{EchoLauncherMarker, EchoMarker};
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use futures::join;
use futures::prelude::*;

// [START main]
#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let echo_launcher =
        connect_to_service::<EchoLauncherMarker>().context("Failed to connect to echo service")?;

    // Create a future that obtains an Echo protocol using the non-pipelined
    // GetEcho method
    let non_pipelined_fut =
        echo_launcher.get_echo("not pipelined: ").and_then(|client_end| async move {
            // "Upgrade" the client end in the response into an Echo proxy, and
            // make an EchoString request on it
            let proxy = client_end.into_proxy()?;
            proxy.echo_string("hello").map_ok(|val| println!("Got echo response {}", val)).await
        });

    // Create a future that obtains an Echo protocol using the pipelined GetEcho
    // method
    let (proxy, server_end) = create_proxy::<EchoMarker>()?;
    echo_launcher.get_echo_pipelined("pipelined: ", server_end)?;
    // We can make a request to the server right after sending the pipelined request
    let pipelined_fut =
        proxy.echo_string("hello").map_ok(|val| println!("Got echo response {}", val));

    // Run the two futures to completion
    let (non_pipelined_result, pipelined_result) = join!(non_pipelined_fut, pipelined_fut);
    pipelined_result?;
    non_pipelined_result?;
    Ok(())
}
// [END main]
