// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl, fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::client, fuchsia_syslog as syslog, fuchsia_zircon as zx, futures::prelude::*,
    log::*, matches::assert_matches,
};

#[fasync::run_singlethreaded]
async fn main() {
    syslog::init_with_tags(&["routing_failed_echo_client"]).expect("failed to init logger");

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
    let echo = client::connect_to_service::<fecho::EchoMarker>().expect("error connecting to echo");
    let err =
        echo.echo_string(Some("Hippos rule!")).await.expect_err("echo_string should have failed");
    let epitaph = echo.take_event_stream().next().await.expect("no epitaph");
    info!("Connecting to Echo protocol failed with error \"{}\" and epitaph {:?}", err, epitaph);
    assert_matches!(
        err,
        fidl::Error::ClientWrite(zx::Status::PEER_CLOSED)
            | fidl::Error::ClientChannelClosed { status: zx::Status::UNAVAILABLE, .. }
    );
    assert_matches!(
        epitaph,
        Err(fidl::Error::ClientChannelClosed { status: zx::Status::UNAVAILABLE, .. })
    );

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
    let echo2 = client::connect_to_service_at_path::<fecho::EchoMarker>(
        "/svc/fidl.examples.routing.echo.Echo2",
    )
    .expect("error connecting to echo");
    let err =
        echo2.echo_string(Some("Hippos rule!")).await.expect_err("echo_string should have failed");
    info!("Connecting to Echo2 protocol failed with error \"{}\"", err);
    assert_matches!(
        err,
        fidl::Error::ClientWrite(zx::Status::PEER_CLOSED)
            | fidl::Error::ClientChannelClosed { status: zx::Status::PEER_CLOSED, .. }
    );
    assert_matches!(echo2.take_event_stream().next().await, None);
}
