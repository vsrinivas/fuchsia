// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async as fasync,
    io_util::{open_directory_in_namespace, OPEN_RIGHT_READABLE},
    std::cmp::min,
    test_utils_lib::{events::*, opaque_test::*},
};

/// Drains the required number of events, sorts them and compares them
/// to the expected events
fn expect_next(actual_events: &mut Vec<EventDescriptor>, expected_events: Vec<EventMatcher>) {
    let num_events: usize = min(expected_events.len(), actual_events.len());
    let mut actual_events: Vec<EventDescriptor> = actual_events.drain(0..num_events).collect();
    actual_events.sort_unstable();
    for (actual_event, expected_event) in actual_events.iter().zip(expected_events.iter()) {
        expected_event.matches(actual_event).unwrap();
    }
}

#[fasync::run_singlethreaded(test)]
async fn destruction() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/destruction_integration_test#meta/collection_realm.cm",
    )
    .await
    .unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();

    let event_log = event_source.record_events(vec![Stopped::NAME, Destroyed::NAME]).await.unwrap();
    let mut event_stream = event_source.subscribe(vec![Destroyed::NAME]).await.unwrap();
    event_source.start_component_tree().await;

    // Wait for `coll:parent` to be destroyed.
    let event = event_stream
        .wait_until_exact::<Destroyed>(EventMatcher::new().expect_moniker("./coll:parent:1"))
        .await
        .unwrap();

    // Assert that parent component has no children.
    let child_dir_path = test.get_hub_v2_path().join("children");
    let child_dir_path = child_dir_path.to_str().expect("invalid chars");
    let child_dir = open_directory_in_namespace(child_dir_path, OPEN_RIGHT_READABLE).unwrap();
    let child_dir_contents = list_directory(&child_dir).await.unwrap();
    assert!(child_dir_contents.is_empty());

    // Assert the expected lifecycle events. The leaves can be stopped/destroyed in either order.
    let mut events = event_log.flush().await;

    expect_next(
        &mut events,
        vec![
            EventMatcher::new()
                .expect_type(Stopped::TYPE)
                .expect_moniker("./coll:parent:1/trigger_a:0"),
            EventMatcher::new()
                .expect_type(Stopped::TYPE)
                .expect_moniker("./coll:parent:1/trigger_b:0"),
        ],
    );

    expect_next(
        &mut events,
        vec![EventMatcher::new().expect_type(Stopped::TYPE).expect_moniker("./coll:parent:1")],
    );

    expect_next(
        &mut events,
        vec![
            EventMatcher::new()
                .expect_type(Destroyed::TYPE)
                .expect_moniker("./coll:parent:1/trigger_a:0"),
            EventMatcher::new()
                .expect_type(Destroyed::TYPE)
                .expect_moniker("./coll:parent:1/trigger_b:0"),
        ],
    );

    expect_next(
        &mut events,
        vec![EventMatcher::new().expect_type(Destroyed::TYPE).expect_moniker("./coll:parent:1")],
    );

    assert!(events.is_empty());
    event.resume().await.unwrap();
}
