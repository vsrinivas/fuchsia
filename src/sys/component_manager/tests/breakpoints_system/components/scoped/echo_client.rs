// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err, fidl_fidl_examples_routing_echo as fecho,
    fidl_fidl_test_components as ftest, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
};

#[fasync::run_singlethreaded]
async fn main() {
    // This client:
    // 1.  Sends out a start trigger to `echo_reporter`.
    // 2.  Connects to `echo_server`.
    // 3.  Issues 10 echos to `echo_server`.
    // 4.  Sends an end trigger to `echo_reporter`.
    // `echo_reporter` begins tracking `RouteCapability` events after step 1.
    // It is expected `echo_reporter` will not see `echo_client` attempting
    // to connect to `echo_server` because it is scoped to events from realms.
    let trigger =
        connect_to_service::<ftest::TriggerMarker>().expect("error connecting to trigger");

    // Trigger start
    trigger.run().await.expect("start trigger failed");

    let echo = connect_to_service::<fecho::EchoMarker>().expect("error connecting to echo");

    for _ in 1..=10 {
        let out = echo.echo_string(Some("Hippos rule!")).await.expect("echo_string failed");
        let out = out.ok_or(format_err!("empty result")).expect("echo_string got empty result");
        println!("Sent \"Hippos rule!\". Received \"{}\"", out);
    }

    // Trigger end
    trigger.run().await.expect("end trigger failed");
}
