// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    test_utils_lib::{events::*, opaque_test::*},
    work_scheduler_dispatch_reporter::{DispatchedEvent, WorkSchedulerDispatchReporter},
};

#[fuchsia_async::run_singlethreaded(test)]
async fn basic_work_scheduler_test() {
    let root_component_url =
        "fuchsia-pkg://fuchsia.com/work_scheduler_integration_test#meta/bound_worker.cm";
    let test = OpaqueTest::default(root_component_url).await.unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();
    let mut event_stream = event_source.subscribe(vec![Started::NAME]).await.unwrap();

    let work_scheduler_dispatch_reporter = WorkSchedulerDispatchReporter::new();
    event_source.install_injector(work_scheduler_dispatch_reporter.clone(), None).await.unwrap();

    event_source.start_component_tree().await;

    // Expect the root component to be bound to
    let event = event_stream.expect_match::<Started>(EventMatcher::ok().expect_moniker(".")).await;
    event.resume().await.unwrap();

    let dispatched_event = work_scheduler_dispatch_reporter.wait_for_dispatched().await;
    assert_eq!(DispatchedEvent::new("TEST".to_string()), dispatched_event);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn unbound_work_scheduler_test() {
    let root_component_url =
        "fuchsia-pkg://fuchsia.com/work_scheduler_integration_test#meta/unbound_child_worker_parent.cm";
    let test = OpaqueTest::default(root_component_url).await.unwrap();

    let event_source = test.connect_to_event_source().await.unwrap();
    let mut event_stream = event_source.subscribe(vec![Started::NAME]).await.unwrap();

    let work_scheduler_dispatch_reporter = WorkSchedulerDispatchReporter::new();
    event_source.install_injector(work_scheduler_dispatch_reporter.clone(), None).await.unwrap();

    event_source.start_component_tree().await;

    // Expect the root component to be bound to
    let event = event_stream.expect_match::<Started>(EventMatcher::ok().expect_moniker(".")).await;
    event.resume().await.unwrap();

    // `/worker_sibling:0` has started.
    let event = event_stream
        .expect_match::<Started>(EventMatcher::ok().expect_moniker("./worker_sibling:0"))
        .await;
    event.resume().await.unwrap();

    // We no longer need to track `StartInstance` events.
    drop(event_stream);

    let dispatched_event = work_scheduler_dispatch_reporter.wait_for_dispatched().await;
    assert_eq!(DispatchedEvent::new("TEST".to_string()), dispatched_event);
}
