// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::*, matcher::*},
    fuchsia_async as fasync,
    test_utils_lib::opaque_test::*,
};

#[fasync::run_singlethreaded(test)]
async fn storage() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/storage_integration_test#meta/storage_realm.cm",
    )
    .await
    .unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();
    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Stopped::NAME], EventMode::Async)])
        .await
        .unwrap();

    event_source.start_component_tree().await;

    // Expect the static child to stop
    EventMatcher::ok()
        .stop(Some(ExitStatusMatcher::Clean))
        .moniker("./storage_user:0")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}

#[fasync::run_singlethreaded(test)]
async fn storage_from_collection() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/storage_integration_test#meta/storage_realm_coll.cm",
    )
    .await
    .unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();
    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Stopped::NAME], EventMode::Async)])
        .await
        .unwrap();

    event_source.start_component_tree().await;

    // Expect the root to stop
    EventMatcher::ok()
        .stop(Some(ExitStatusMatcher::Clean))
        .moniker("./")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}

#[fasync::run_singlethreaded(test)]
async fn storage_from_collection_with_invalid_route() {
    let test = OpaqueTest::default(
        "fuchsia-pkg://fuchsia.com/storage_integration_test#meta/storage_realm_coll_invalid_route.cm",
    )
    .await
    .unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();
    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Stopped::NAME], EventMode::Async)])
        .await
        .unwrap();

    event_source.start_component_tree().await;

    // Expect the root to stop
    EventMatcher::ok()
        .stop(Some(ExitStatusMatcher::Clean))
        // TODO(81348): use the same value here to reference the root moniker as the values used
        // elsewhere in this file.
        .moniker(".")
        .wait::<Stopped>(&mut event_stream)
        .await
        .unwrap();
}
