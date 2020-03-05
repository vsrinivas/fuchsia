// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_syslog as syslog,
    log::*,
    test_utils_lib::events::{Destroyed, Event, EventSource, Ordering, RecordedEvent},
};

#[fasync::run_singlethreaded]
async fn main() {
    syslog::init_with_tags(&[]).expect("could not initialize logging");
    info!("Realm started");
    run_tests().await.expect("Test failed");
    println!("Done");
}

async fn run_tests() -> Result<(), Error> {
    let event_source = EventSource::new()?;
    // Creating children will not complete until `start_component_tree` is called.
    event_source.start_component_tree().await?;
    test_scoped_instance(&event_source).await?;
    Ok(())
}

async fn test_scoped_instance(event_source: &EventSource) -> Result<(), Error> {
    let event = RecordedEvent {
        event_type: Destroyed::TYPE,
        target_moniker: "./coll:auto-*".to_string(),
        capability_id: None,
    };
    let expected_events: Vec<_> = (0..3).map(|_| event.clone()).collect();
    let expectation = event_source.expect_events(Ordering::Unordered, expected_events).await?;
    let mut instances = vec![];
    for _ in 1..=3 {
        let url =
            "fuchsia-pkg://fuchsia.com/fuchsia-component-tests#meta/echo_server.cm".to_string();
        let scoped_instance = client::create_scoped_dynamic_instance("coll".to_string(), url)
            .await
            .context("instance creation failed")?;
        let echo_proxy = scoped_instance
            .connect_to_protocol_at_exposed_dir::<fecho::EchoMarker>()
            .context("failed to connect to echo in exposed dir")?;
        let out = echo_proxy
            .echo_string(Some("hippos"))
            .await
            .context("echo_string failed")?
            .ok_or(format_err!("empty result"))?;
        assert_eq!(out, "hippos");
        instances.push(scoped_instance);
    }

    // Dropping the scoped instances should cause them to all be destroyed.
    drop(instances);
    expectation.await?;
    Ok(())
}
