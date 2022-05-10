// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use config_lib::Config;
use fidl_fidl_examples_routing_echo::EchoMarker;
use fuchsia_component::client::connect_to_protocol;

#[fuchsia::main]
async fn main() {
    let Config { echo_string, echo_string_vector, echo_bool, echo_num } =
        Config::take_from_startup_handle();

    // Connect to FIDL protocol
    let echo = connect_to_protocol::<EchoMarker>().expect("error connecting to echo");

    let output = format!(
        "{}, {}, {}, {}, {}",
        echo_string, echo_string_vector[0], echo_string_vector[1], echo_bool, echo_num
    );

    // Send the echo string over FIDL interface
    echo.echo_string(Some(&output)).await.expect("echo_string failed");
}
