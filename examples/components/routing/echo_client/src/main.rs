// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START imports]
use anyhow;
use fidl_fidl_examples_routing_echo::EchoMarker;
use fuchsia_component::client::connect_to_protocol;
use tracing;
// [END imports]

// [START main_body]
#[fuchsia::component]
async fn main() -> Result<(), anyhow::Error> {
    // Parse arguments, removing binary name
    let mut args: Vec<String> = std::env::args().collect();
    args.remove(0);

    // Connect to FIDL protocol
    let echo = connect_to_protocol::<EchoMarker>().expect("error connecting to echo");

    // Send messages over FIDL interface
    for message in args {
        let out = echo.echo_string(Some(&message)).await.expect("echo_string failed");
        tracing::info!("Server response: {}", out.as_ref().expect("echo_string got empty result"));
    }

    Ok(())
}
// [END main_body]
