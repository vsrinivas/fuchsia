// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{format_err, Context, Error},
    component_events::{
        events::*,
        matcher::EventMatcher,
        sequence::{EventSequence, Ordering},
    },
    fidl_fidl_examples_routing_echo as fecho,
    fuchsia_component::client::connect_to_childs_protocol,
    fuchsia_component_test::ScopedInstance,
    futures::future::join_all,
    tracing::*,
};

#[derive(argh::FromArgs)]
/// Helper binary to test binding to components v2 children.
struct Args {
    /// whether or not to wait on ScopedInstance destroy waiters.
    #[argh(switch, long = "wait")]
    wait: bool,
}

#[fuchsia::main]
async fn main() {
    let Args { wait } = argh::from_env();
    info!("Realm started");

    let event_stream = EventStream::open().await.unwrap();
    // Create 3 scoped instances
    let mut instances = create_instances().await.expect("failed to create instances");
    info!("Created instances");

    let waiters = if wait {
        // Grab the destroy waiters for each scoped instance, each of which will resolve once
        // destruction for its instance is complete
        let waiters: Vec<_> = instances.iter_mut().map(|i| i.take_destroy_waiter()).collect();
        Some(waiters)
    } else {
        None
    };

    // Drop the ScopedInstances, which will cause the child components to be destroyed
    info!("Dropping scoped instances");
    drop(instances);

    if let Some(waiters) = waiters {
        info!("Waiting on destroy waiters");
        // Wait for all of the instances to be destroyed, assert that there were no errors
        for result in join_all(waiters).await {
            result.expect("destruction failed");
        }
    }

    EventSequence::new()
        .all_of(
            vec![
                EventMatcher::ok()
                    .r#type(Destroyed::TYPE)
                    .moniker("./coll:static_name".to_string()),
                EventMatcher::ok()
                    .r#type(Destroyed::TYPE)
                    .moniker_regex("./coll:auto-.*".to_string()),
                EventMatcher::ok()
                    .r#type(Destroyed::TYPE)
                    .moniker_regex("./coll:auto-.*".to_string()),
                EventMatcher::ok()
                    .r#type(Destroyed::TYPE)
                    .moniker_regex("./coll:auto-.*".to_string()),
            ],
            Ordering::Unordered,
        )
        .expect(event_stream)
        .await
        .unwrap();
}

async fn create_instances() -> Result<Vec<ScopedInstance>, Error> {
    let url = "#meta/echo_server.cm".to_string();
    // Create 4 scoped instances, and confirm that each is functioning correctly by using a FIDL
    // service from it
    let instances = vec![
        ScopedInstance::new("coll".to_string(), url.clone())
            .await
            .context("instance creation failed")?,
        ScopedInstance::new("coll".to_string(), url.clone())
            .await
            .context("instance creation failed")?,
        ScopedInstance::new("coll".to_string(), url.clone())
            .await
            .context("instance creation failed")?,
        ScopedInstance::new_with_name("static_name".to_string(), "coll".to_string(), url.clone())
            .await
            .context("instance creation failed")?,
    ];
    for scoped_instance in instances.iter() {
        {
            let echo_proxy = scoped_instance
                .connect_to_protocol_at_exposed_dir::<fecho::EchoMarker>()
                .context("failed to connect to echo in exposed dir")?;
            let out = echo_proxy
                .echo_string(Some("hippos"))
                .await
                .context("echo_string failed")?
                .ok_or(format_err!("empty result"))?;
            assert_eq!(out, "hippos");
        }
        {
            let echo_proxy = connect_to_childs_protocol::<fecho::EchoMarker>(
                scoped_instance.child_name().to_string(),
                Some("coll".to_string()),
            )
            .await
            .context("failed to connect to child's echo service")?;
            let out = echo_proxy
                .echo_string(Some("hippos"))
                .await
                .context("echo_string failed")?
                .ok_or(format_err!("empty result"))?;
            assert_eq!(out, "hippos");
        }
    }
    Ok(instances)
}
