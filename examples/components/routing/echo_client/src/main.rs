// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow;
use fuchsia_async as fasync;
use fuchsia_inspect::{component, health::Reporter};
use tracing;
// [START imports]
use fidl_fidl_examples_routing_echo::EchoMarker;
use fuchsia_component::client::connect_to_protocol;
// [END imports]

#[fasync::run_singlethreaded]
// [START main_body]
#[fuchsia::component(logging = true)]
async fn main() -> Result<(), anyhow::Error> {
    component::health().set_starting_up();

    // Parse arguments, removing binary name
    let mut args: Vec<String> = std::env::args().collect();
    args.remove(0);
    let message = args.join(" ");

    // Connect to FIDL protocol
    let echo = connect_to_protocol::<EchoMarker>().expect("error connecting to echo");

    component::health().set_ok();
    tracing::debug!("Initialized.");

    // Send message over FIDL interface
    let out = echo.echo_string(Some(&message)).await.expect("echo_string failed");
    tracing::info!("Server response: {}", out.as_ref().expect("echo_string got empty result"));

    Ok(())
}
// [END main_body]
