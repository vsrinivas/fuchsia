// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_async as fasync,
    io_util::{open_directory_in_namespace, OPEN_RIGHT_READABLE},
    std::cmp::min,
    test_utils_lib::{events::*, test_utils::*},
};

/// Drains the required number of events, sorts them and compares them
/// to the expected events
fn expect_next(events: &mut Vec<RecordedEvent>, expected: Vec<RecordedEvent>) {
    let num_events: usize = min(expected.len(), events.len());
    let mut next: Vec<RecordedEvent> = events.drain(0..num_events).collect();
    next.sort_unstable();
    assert_eq!(next, expected);
}

#[fasync::run_singlethreaded(test)]
async fn destruction() -> Result<(), Error> {
    let test = BlackBoxTest::default(
        "fuchsia-pkg://fuchsia.com/destruction_integration_test#meta/collection_realm.cm",
    )
    .await?;

    let event_source = test.connect_to_event_source().await?;

    let sink = event_source.soak_events(vec![Stopped::TYPE, Destroyed::TYPE]).await?;
    let mut event_stream = event_source.subscribe(vec![Destroyed::TYPE]).await?;
    event_source.start_component_tree().await?;

    // Wait for `coll:root` to be destroyed.
    let event = event_stream.wait_until_exact::<Destroyed>("./coll:root:1").await?;

    // Assert that root component has no children.
    let child_dir_path = test.get_hub_v2_path().join("children");
    let child_dir_path = child_dir_path.to_str().expect("invalid chars");
    let child_dir = open_directory_in_namespace(child_dir_path, OPEN_RIGHT_READABLE)?;
    let child_dir_contents = list_directory(&child_dir).await?;
    assert!(child_dir_contents.is_empty());

    // Assert the expected lifecycle events. The leaves can be stopped/destroyed in either order.
    let mut events = sink.drain().await;

    expect_next(
        &mut events,
        vec![
            RecordedEvent {
                event_type: Stopped::TYPE,
                target_moniker: "./coll:root:1/trigger_a:0".to_string(),
                capability_id: None,
            },
            RecordedEvent {
                event_type: Stopped::TYPE,
                target_moniker: "./coll:root:1/trigger_b:0".to_string(),
                capability_id: None,
            },
        ],
    );

    expect_next(
        &mut events,
        vec![RecordedEvent {
            event_type: Stopped::TYPE,
            target_moniker: "./coll:root:1".to_string(),
            capability_id: None,
        }],
    );

    expect_next(
        &mut events,
        vec![
            RecordedEvent {
                event_type: Destroyed::TYPE,
                target_moniker: "./coll:root:1/trigger_a:0".to_string(),
                capability_id: None,
            },
            RecordedEvent {
                event_type: Destroyed::TYPE,
                target_moniker: "./coll:root:1/trigger_b:0".to_string(),
                capability_id: None,
            },
        ],
    );

    expect_next(
        &mut events,
        vec![RecordedEvent {
            event_type: Destroyed::TYPE,
            target_moniker: "./coll:root:1".to_string(),
            capability_id: None,
        }],
    );

    assert!(events.is_empty());
    event.resume().await?;

    Ok(())
}
