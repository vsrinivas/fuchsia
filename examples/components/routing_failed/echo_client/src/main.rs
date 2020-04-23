// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl, fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service, fuchsia_zircon as zx, futures::prelude::*,
    matches::assert_matches,
};

#[fasync::run_singlethreaded]
async fn main() {
    let echo = connect_to_service::<fecho::EchoMarker>().expect("error connecting to echo");
    // The `echo` channel should be closed with an epitaph because routing failed (see
    // echo_realm.cml)
    //
    // The epiapth itself is just a zx_status_t. To get detailed information about why the routing
    // failed, you'll need to check the kernel debuglog. Look for a message like this:
    //
    // > [component_manager] ERROR: Failed to route protocol
    // > `/svc/fidl.examples.routing.echo.Echo` from component `/echo_client:0`: A `use from
    // > realm` declaration was found at `/echo_client:0` for
    // > `/svc/fidl.examples.routing.echo.Echo`, but no matching `offer` declaration was found in
    // > the parent
    echo.echo_string(Some("Hippos rule!")).await.expect_err("echo_string should have failed");
    let epitaph = echo.take_event_stream().next().await.expect("no epitaph");
    assert_matches!(epitaph, Err(fidl::Error::ClientChannelClosed(zx::Status::UNAVAILABLE)));
}
