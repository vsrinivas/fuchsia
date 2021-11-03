// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_events::{events::*, matcher::*, sequence::*},
    test_utils_lib::opaque_test::*,
};

#[fuchsia_async::run_singlethreaded(test)]
async fn basic_work_scheduler_test() {
    let root_component_url =
        "fuchsia-pkg://fuchsia.com/work_scheduler_integration_test#meta/bound_worker.cm";
    let test = OpaqueTest::default(root_component_url).await.unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();
    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Stopped::NAME], EventMode::Async)])
        .await
        .unwrap();

    event_source.start_component_tree().await;

    // Expect the root component to exit cleanly
    EventMatcher::ok()
        .stop(Some(ExitStatusMatcher::Clean))
        .moniker_regex(".")
        .expect_match::<Stopped>(&mut event_stream)
        .await;
}

#[fuchsia_async::run_singlethreaded(test)]
async fn unbound_work_scheduler_test() {
    let root_component_url =
        "fuchsia-pkg://fuchsia.com/work_scheduler_integration_test#meta/unbound_child_worker_parent.cm";
    let test = OpaqueTest::default(root_component_url).await.unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();
    let event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Stopped::NAME], EventMode::Async)])
        .await
        .unwrap();

    event_source.start_component_tree().await;

    // Expect both components to exit cleanly
    EventSequence::new()
        .all_of(
            vec![
                EventMatcher::ok()
                    .stop(Some(ExitStatusMatcher::Clean))
                    .moniker_regex("./worker_sibling:0"),
                EventMatcher::ok()
                    .stop(Some(ExitStatusMatcher::Clean))
                    .moniker_regex("./worker_child:0"),
            ],
            Ordering::Unordered,
        )
        .expect(event_stream)
        .await
        .unwrap();
}
