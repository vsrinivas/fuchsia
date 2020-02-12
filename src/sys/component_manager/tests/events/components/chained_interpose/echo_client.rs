// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, fidl_fidl_examples_routing_echo as fecho,
    fidl_fuchsia_test_echofactory as fechofactory, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let echo_factory = connect_to_service::<fechofactory::EchoFactoryMarker>()?;

    // Create the first echo channel.
    let (proxy_a, server_end_a) = fidl::endpoints::create_proxy::<fecho::EchoMarker>()?;
    echo_factory.request_echo_protocol(server_end_a).await?;
    proxy_a.echo_string(Some("a")).await?;

    // Create the second echo channel.
    let (proxy_b, server_end_b) = fidl::endpoints::create_proxy::<fecho::EchoMarker>()?;
    echo_factory.request_echo_protocol(server_end_b).await?;
    proxy_b.echo_string(Some("b")).await?;

    // Create the third echo channel.
    let (proxy_c, server_end_c) = fidl::endpoints::create_proxy::<fecho::EchoMarker>()?;
    echo_factory.request_echo_protocol(server_end_c).await?;
    proxy_c.echo_string(Some("c")).await?;

    Ok(())
}
