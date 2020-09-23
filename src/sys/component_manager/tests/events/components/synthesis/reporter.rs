// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::client::{self as component, ScopedInstance},
    regex::Regex,
    std::{collections::BTreeSet, convert::TryFrom, iter::FromIterator},
    test_utils_lib::events::{
        CapabilityReady, Event, EventSource, MarkedForDestruction, Running, Started,
    },
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut instances = vec![];
    let url =
        "fuchsia-pkg://fuchsia.com/events_integration_test#meta/stub_component.cm".to_string();
    let url_cap_ready =
        "fuchsia-pkg://fuchsia.com/events_integration_test#meta/capability_ready_child.cm"
            .to_string();
    let scoped_instance = ScopedInstance::new("coll".to_string(), url_cap_ready.clone()).await?;
    instances.push(scoped_instance);
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
        .subscribe(vec![
            Started::NAME,
            Running::NAME,
            MarkedForDestruction::NAME,
            CapabilityReady::NAME,
        ])
        .await?;

    let echo = component::connect_to_service::<fecho::EchoMarker>()?;

    // There were 4 running instances when the stream was created: this instance itself and three
    // more. We are also expecting capability ready for one of them.
    let mut running = vec![];
    let mut capability_ready = BTreeSet::new();

    while running.len() != 4 || capability_ready.len() != 1 {
        let event = event_stream.next().await?;
        match event.event_type {
            Some(Running::TYPE) => {
                let event = Running::try_from(event).expect("convert to running");
                running.push(event.target_moniker().to_string());
            }
            Some(CapabilityReady::TYPE) => {
                let event = CapabilityReady::try_from(event).expect("convert to capability ready");
                capability_ready.insert(event.target_moniker().to_string());
            }
            other => panic!("unexpected event type: {:?}", other),
        }
    }

    for _ in &running {
        let _ = echo.echo_string(Some(&format!("{:?}", Running::TYPE))).await?;
    }
    for _ in &capability_ready {
        let _ = echo.echo_string(Some(&format!("{:?}", CapabilityReady::TYPE))).await?;
    }

    assert_eq!(running.len(), 4);
    assert_eq!(capability_ready.len(), 1);
    assert_eq!(running[0], ".");
    let re = Regex::new(r"./coll:auto-\d+:\d").unwrap();
    assert!(running[1..].iter().all(|m| re.is_match(m)));
    assert_eq!(BTreeSet::from_iter::<Vec<String>>(running).len(), 4);

    // Dropping instances stops and destroys the children.
    drop(instances);

    // The three instances were marked for destruction.
    let mut seen_marked_for_destruction = 0;
    while seen_marked_for_destruction != 3 {
        let event = event_stream.next().await?;
        match event.event_type {
            Some(CapabilityReady::TYPE) => {
                // ignore. we could get a duplicate here.
            }
            Some(MarkedForDestruction::TYPE) => {
                let _ =
                    echo.echo_string(Some(&format!("{:?}", MarkedForDestruction::TYPE))).await?;
                seen_marked_for_destruction += 1;
            }
            event => {
                panic!("Got unexpected event type: {:?}", event);
            }
        }
    }

    Ok(())
}
