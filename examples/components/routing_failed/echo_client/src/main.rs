// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl, fidl_fidl_examples_routing_echo as fecho, fuchsia_component::client,
    fuchsia_zircon as zx, matches::assert_matches, tracing::info,
};

#[fuchsia::component]
async fn main() {
    // The `echo` channel should be closed with an epitaph because routing failed (see
    // echo_realm.cml)
    //
    // The epitaph itself is just a zx_status_t. To get detailed information about why the routing
    // failed, you'll need to check the kernel debuglog. Look for a message like this:
    //
    // > [component_manager] ERROR: Failed to route protocol
    // > `/svc/fidl.examples.routing.echo.Echo` from component `/echo_client:0`: A `use from
    // > realm` declaration was found at `/echo_client:0` for
    // > `/svc/fidl.examples.routing.echo.Echo`, but no matching `offer` declaration was found in
    // > the parent
    let echo =
        client::connect_to_protocol::<fecho::EchoMarker>().expect("error connecting to echo");
    let err =
        echo.echo_string(Some("Hippos rule!")).await.expect_err("echo_string should have failed");
    info!("Connecting to Echo protocol failed with error \"{}\"", err);
    assert_matches!(err, fidl::Error::ClientChannelClosed { status: zx::Status::UNAVAILABLE, .. });

    // The `echo2` channel should be closed because routing succeeded but the runner failed to
    // start the component. The channel won't have an epitaph set; the runner closes the source
    // component's outgoing directory request handle and that causes the channel for the service
    // connection to be closed as well. In the kernel debuglog, look for a message like this:
    //
    // > [component_manager] ERROR: Failed to start component
    // > `fuchsia-pkg://fuchsia.com/components-routing-failed-example#meta/echo_server_bad.cm`:
    // > unable to load component with
    // > url "fuchsia-pkg://fuchsia.com/components-routing-failed-example#meta/echo_server_bad.cm":
    // > error loading executable: "reading object at "bin/routing_failed_echo_server_oops" failed:
    // > A FIDL client's channel was closed: PEER_CLOSED"
    let echo2 = client::connect_to_protocol_at_path::<fecho::EchoMarker>(
        "/svc/fidl.examples.routing.echo.Echo2",
    )
    .expect("error connecting to echo");
    let err =
        echo2.echo_string(Some("Hippos rule!")).await.expect_err("echo_string should have failed");
    info!("Connecting to Echo2 protocol failed with error \"{}\"", err);
    assert_matches!(err, fidl::Error::ClientChannelClosed { status: zx::Status::PEER_CLOSED, .. });
}
