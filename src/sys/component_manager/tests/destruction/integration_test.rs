// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async as fasync,
    io_util::{open_directory_in_namespace, OPEN_RIGHT_READABLE},
    test_utils_lib::{
        events::*,
        matcher::EventMatcher,
        opaque_test::*,
        sequence::{EventSequence, Ordering},
    },
};

#[fasync::run_singlethreaded(test)]
async fn destruction() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/destruction_integration_test#meta/collection_realm.cm",
    )
    .await
    .unwrap();

    let mut event_source = test.connect_to_event_source().await.unwrap();

    let mut event_stream = event_source.subscribe(vec![Destroyed::NAME]).await.unwrap();
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
                EventMatcher::ok().r#type(Destroyed::TYPE).moniker("./coll:parent:1/trigger_a:0"),
                EventMatcher::ok().r#type(Destroyed::TYPE).moniker("./coll:parent:1/trigger_b:0"),
            ],
            Ordering::Unordered,
        )
        .then(EventMatcher::ok().r#type(Destroyed::TYPE).moniker("./coll:parent:1"))
        .subscribe_and_expect(&mut event_source)
        .await
        .unwrap();
    event_source.start_component_tree().await;

    // Wait for `coll:parent` to be destroyed.
    let event = EventMatcher::ok()
        .moniker("./coll:parent:1$")
        .wait::<Destroyed>(&mut event_stream)
        .await
        .unwrap();

    // Assert that parent component has no children.
    let child_dir_path = test.get_hub_v2_path().join("children");
    let child_dir_path = child_dir_path.to_str().expect("invalid chars");
    let child_dir = open_directory_in_namespace(child_dir_path, OPEN_RIGHT_READABLE).unwrap();
    let child_dir_contents = list_directory(&child_dir).await.unwrap();
    assert!(child_dir_contents.is_empty());

    // Assert the expected lifecycle events. The leaves can be stopped/destroyed in either order.
    event.resume().await.unwrap();
    expectation.await.unwrap();
}
