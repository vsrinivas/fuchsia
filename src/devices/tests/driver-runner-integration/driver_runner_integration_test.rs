// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{
        descriptor::EventDescriptor,
        events::{self, Event},
        matcher::EventMatcher,
    },
    fuchsia_async as fasync,
    fuchsia_component_test::builder::{
        Capability, CapabilityRoute, ComponentSource, RealmBuilder, RouteEndpoint,
    },
    std::convert::TryFrom,
};

// This will only return once all `events` are seen in `event_stream`.
// The events do not have to happen in order.
// Extra events will be discarded.
async fn check_events(
    mut events: Vec<EventMatcher>,
    event_stream: &mut events::EventStream,
) -> Result<(), anyhow::Error> {
    while events.len() != 0 {
        let next = event_stream.next().await?;
        let next = EventDescriptor::try_from(&next)?;
        events.retain(|event| event.matches(&next).is_err());
    }
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn driver_runner_test() -> Result<(), anyhow::Error> {
    // Set up our expected events.
    let event_source = events::EventSource::new()?;
    let mut started_stream = event_source
        .subscribe(vec![events::EventSubscription::new(
            vec![events::Started::NAME],
            events::EventMode::Async,
        )])
        .await?;

    let mut builder = RealmBuilder::new().await?;
    builder
        .add_eager_component(
            "root",
            ComponentSource::url(
                "fuchsia-pkg://fuchsia.com/driver-runner-integration-test#meta/driver-runner-integration-root.cm",
            ),
        )
        .await?;

    let root = RouteEndpoint::component("root");
    builder.add_route(CapabilityRoute {
        capability: Capability::protocol("fuchsia.logger.LogSink"),
        source: RouteEndpoint::AboveRoot,
        targets: vec![root.clone()],
    })?;
    builder.add_route(CapabilityRoute {
        capability: Capability::protocol("fuchsia.process.Launcher"),
        source: RouteEndpoint::AboveRoot,
        targets: vec![root.clone()],
    })?;
    builder.add_route(CapabilityRoute {
        capability: Capability::protocol("fuchsia.sys.Launcher"),
        source: RouteEndpoint::AboveRoot,
        targets: vec![root.clone()],
    })?;
    let _realm = builder.build().create().await?;

    // List the components that we expect to be created.
    // We list the components by monikers which are described at:
    // https://fuchsia.dev/fuchsia-src/concepts/components/v2/monikers?hl=en
    // Drivers live in collections, and their monikers will look like:
    //   /boot-drivers:{TOPOLOGICAL_NAME}:{INSTANCE_NUMBER}
    //   /pkg-drivers:{TOPOLOGICAL_NAME}:{INSTANCE_NUMBER}
    // Driver hosts live in a collection, and their monikers will look like:
    //   /driver_hosts:driver_host-{DRIVER_NUMBER}:{INSTANCE_NUMBER}
    // We don't know how consistent the INSTANCE_NUMBER is so we regex match it with '\d+'.
    let events = vec![
        EventMatcher::ok().r#type(events::Started::TYPE).moniker(r".*/driver_manager:\d+"),
        EventMatcher::ok().r#type(events::Started::TYPE).moniker(r".*/driver_index:\d+"),
        EventMatcher::ok().r#type(events::Started::TYPE).moniker(r".*/pkg-drivers:root:\d+"),
        EventMatcher::ok()
            .r#type(events::Started::TYPE)
            .moniker(r".*/driver_hosts:driver_host-0:\d+"),
    ];
    check_events(events, &mut started_stream).await?;

    Ok(())
}
