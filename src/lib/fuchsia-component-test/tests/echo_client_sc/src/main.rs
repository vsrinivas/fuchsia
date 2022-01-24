// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use config_lib::Config;
use fidl_fidl_examples_routing_echo::EchoMarker;
use fuchsia_component::client::connect_to_protocol;

#[fuchsia::component]
async fn main() {
    let Config { echo_string } = Config::from_args();

    // Connect to FIDL protocol
    let echo = connect_to_protocol::<EchoMarker>().expect("error connecting to echo");

    // Send the echo string over FIDL interface
    echo.echo_string(Some(&echo_string)).await.expect("echo_string failed");
}
