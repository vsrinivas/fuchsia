// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    assert_matches::assert_matches, fidl, fidl_fidl_examples_routing_echo as fecho,
    fuchsia_component::client, fuchsia_zircon as zx, tracing::info,
};

#[fuchsia::main]
async fn main() {
    // The `echo` channel should be closed with an epitaph because routing failed (see
    // echo_realm.cml)
    //
    // The epitaph itself is just a zx_status_t. To get detailed information about why the routing
    // failed, you'll need to check the kernel debuglog.
    let echo =
        client::connect_to_protocol::<fecho::EchoMarker>().expect("error connecting to echo");
    let err =
        echo.echo_string(Some("Hippos rule!")).await.expect_err("echo_string should have failed");
    info!("Connecting to Echo protocol failed with error \"{}\"", err);
    assert_matches!(err, fidl::Error::ClientChannelClosed { status: zx::Status::UNAVAILABLE, .. });

    // The `echo2` channel should be closed because routing succeeded but the runner failed to
    // start the component. The channel won't have an epitaph set; the runner closes the source
    // component's outgoing directory request handle and that causes the channel for the service
    // connection to be closed as well.
    let echo2 = client::connect_to_protocol_at_path::<fecho::EchoMarker>(
        "/svc/fidl.examples.routing.echo.Echo2",
    )
    .expect("error connecting to echo");
    let err =
        echo2.echo_string(Some("Hippos rule!")).await.expect_err("echo_string should have failed");
    info!("Connecting to Echo2 protocol failed with error \"{}\"", err);
    assert_matches!(err, fidl::Error::ClientChannelClosed { status: zx::Status::PEER_CLOSED, .. });
}
