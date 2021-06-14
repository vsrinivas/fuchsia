// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{
        events::*,
        matcher::EventMatcher,
        sequence::{EventSequence, Ordering},
    },
    fuchsia_async as fasync,
    io_util::{open_directory_in_namespace, OPEN_RIGHT_READABLE},
    test_utils_lib::opaque_test::*,
};

#[fasync::run_singlethreaded(test)]
async fn destroy() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/destruction_integration_test#meta/collection_realm.cm",
    )
    .await
    .unwrap();

    let mut event_source = test.connect_to_event_source().await.unwrap();

    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Purged::NAME], EventMode::Sync)])
        .await
        .unwrap();
    let expectation = EventSequence::new()
        .all_of(
            vec![
                EventMatcher::ok().r#type(Stopped::TYPE).moniker("./coll:parent:1/trigger_a:0"),
                EventMatcher::ok().r#type(Stopped::TYPE).moniker("./coll:parent:1/trigger_b:0"),
            ],
            Ordering::Unordered,
        )
        .then(EventMatcher::ok().r#type(Stopped::TYPE).moniker("./coll:parent:1"))
        .all_of(
            vec![
                EventMatcher::ok().r#type(Purged::TYPE).moniker("./coll:parent:1/trigger_a:0"),
                EventMatcher::ok().r#type(Purged::TYPE).moniker("./coll:parent:1/trigger_b:0"),
            ],
            Ordering::Unordered,
        )
        .then(EventMatcher::ok().r#type(Purged::TYPE).moniker("./coll:parent:1"))
        .subscribe_and_expect(&mut event_source)
        .await
        .unwrap();
    event_source.start_component_tree().await;

    // Wait for `coll:parent` to be purged.
    let event = EventMatcher::ok()
        .moniker("./coll:parent:1$")
        .wait::<Purged>(&mut event_stream)
        .await
        .unwrap();

    // Assert that parent component has no children.
    let child_dir_path = test.get_hub_v2_path().join("children");
    let child_dir_path = child_dir_path.to_str().expect("invalid chars");
    let child_dir = open_directory_in_namespace(child_dir_path, OPEN_RIGHT_READABLE).unwrap();
    let child_dir_contents = list_directory(&child_dir).await.unwrap();
    assert!(child_dir_contents.is_empty());

    // Assert the expected lifecycle events. The leaves can be stopped/purged in either order.
    event.resume().await.unwrap();
    expectation.await.unwrap();
}

#[fasync::run_singlethreaded(test)]
async fn destroy_and_recreate() {
    let mut test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/destruction_integration_test#meta/destroy_and_recreate.cm",
    )
    .await
    .expect("failed to start test");
    let event_source = test.connect_to_event_source().await.unwrap();
    event_source.start_component_tree().await;
    let status = test.component_manager_app.wait().await.expect("failed to wait for component");
    assert!(status.success(), "test failed");
}
