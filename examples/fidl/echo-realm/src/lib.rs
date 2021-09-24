// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    component_events::{events::*, matcher::*},
    fuchsia_component_test::{builder::*, Moniker},
};

#[fuchsia::test]
async fn launch_realm_components() -> Result<(), Error> {
    // Subscribe to started events for child components
    let event_source = EventSource::new().unwrap();
    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(
            vec![Started::NAME, Stopped::NAME],
            EventMode::Async,
        )])
        .await
        .context("failed to subscribe to EventSource")?;

    // Create the test realm,
    let mut builder = RealmBuilder::new().await?;
    builder.add_component(Moniker::root(), ComponentSource::url("#meta/echo_realm.cm")).await?;
    let realm = builder.build();

    // Mark echo_client as eager so it starts automatically.
    realm.mark_as_eager(&"echo_client".into()).await?;

    // Create the realm instance
    let realm_instance = realm.create().await?;

    // Verify that both client and server components started
    EventMatcher::ok()
        .moniker("./echo_client:0")
        .wait::<Started>(&mut event_stream)
        .await
        .context("failed to observe client start")?;
    EventMatcher::ok()
        .moniker("./echo_server:0")
        .wait::<Started>(&mut event_stream)
        .await
        .context("failed to observe server start")?;

    // Verify that the client component exits successfully
    EventMatcher::ok()
        .stop(Some(ExitStatusMatcher::Clean))
        .moniker("./echo_client:0")
        .wait::<Stopped>(&mut event_stream)
        .await
        .context("failed to observe client exit")?;

    // Clean up the realm instance
    realm_instance.destroy().await?;

    Ok(())
}
