// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::client::{self as component, ScopedInstance},
    regex::Regex,
    test_utils_lib::events::{Event, EventSource, MarkedForDestruction, Running, Started},
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut instances = vec![];
    let url =
        "fuchsia-pkg://fuchsia.com/events_integration_test#meta/stub_component.cm".to_string();
    for _ in 0..3 {
        let scoped_instance = ScopedInstance::new("coll".to_string(), url.clone()).await?;
        instances.push(scoped_instance);
    }

    // Destroy one instance, this shouldn't appear anywhere in the events.
    let mut instance = instances.pop().unwrap();
    let destroy_waiter = instance.take_destroy_waiter();
    drop(instance);
    destroy_waiter.await;

    // Subscribe to events.
    let event_source = EventSource::new_async()?;
    let mut event_stream = event_source
        .subscribe(vec![Started::NAME, Running::NAME, MarkedForDestruction::NAME])
        .await?;

    let echo = component::connect_to_service::<fecho::EchoMarker>()?;

    // There were 3 running instances when the stream was created: this instance itself and two
    // more.
    let mut monikers = vec![];
    for _ in 0..3 {
        let event = event_stream.expect_type::<Running>().await?;
        let _ = echo.echo_string(Some(&format!("{:?}", Running::TYPE))).await?;
        monikers.push(event.target_moniker().to_string());
    }
    assert_eq!(monikers[0], ".");
    let re = Regex::new(r"./coll:auto-\d+:\d").unwrap();
    assert!(re.is_match(&monikers[1]));
    assert!(re.is_match(&monikers[2]));
    assert_ne!(monikers[1], monikers[2]);

    // Dropping instances stops and destroys the children.
    drop(instances);

    // The two instances were marked for destruction.
    for _ in 0..2 {
        let _ = event_stream.expect_type::<MarkedForDestruction>().await?;
        let _ = echo.echo_string(Some(&format!("{:?}", MarkedForDestruction::TYPE))).await?;
    }

    Ok(())
}
