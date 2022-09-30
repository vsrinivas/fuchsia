// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    component_events::{events::*, matcher::*},
    fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, Ref, Route},
};

#[fuchsia::test]
async fn launch_realm_components() -> Result<(), Error> {
    // Subscribe to started events for child components
    let mut event_stream = EventStream::open().await.unwrap();

    // Create a new empty test realm
    let builder = RealmBuilder::new().await?;

    // Add the echo server to the realm
    let echo_server =
        builder.add_child("echo_server", "#meta/echo_server.cm", ChildOptions::new()).await?;

    // Add the echo client to the realm, and make the echo_client eager so that it starts
    // automatically
    let echo_client = builder
        .add_child("echo_client", "#meta/echo_client.cm", ChildOptions::new().eager())
        .await?;

    // Route the echo capabilities from the server to the client
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.examples.Echo"))
                .capability(Capability::protocol_by_name("fuchsia.examples.EchoLauncher"))
                .capability(Capability::service_by_name("fuchsia.examples.EchoService"))
                .from(&echo_server)
                .to(&echo_client),
        )
        .await?;

    // Route the LogSink to the server and client, so that both are able to send us logs
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&echo_server)
                .to(&echo_client),
        )
        .await?;

    // Create the realm instance
    let realm_instance = builder.build().await?;

    // Verify that both client and server components started
    EventMatcher::ok()
        .moniker_regex("echo_client$")
        .wait::<Started>(&mut event_stream)
        .await
        .context("failed to observe client start")?;
    EventMatcher::ok()
        .moniker_regex("echo_server$")
        .wait::<Started>(&mut event_stream)
        .await
        .context("failed to observe server start")?;

    // Verify that the client component exits successfully
    EventMatcher::ok()
        .stop(Some(ExitStatusMatcher::Clean))
        .moniker_regex("echo_client$")
        .wait::<Stopped>(&mut event_stream)
        .await
        .context("failed to observe client exit")?;

    // Clean up the realm instance
    realm_instance.destroy().await?;

    Ok(())
}
